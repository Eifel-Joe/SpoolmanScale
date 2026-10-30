#include "services/moonraker_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/backend_http.h"
#include "services/moonraker_parse.h"
#include "services/spoolman_api.h"

namespace {

// ArduinoJson has to be told to use PSRAM, and the allocator must be defined in
// every translation unit that needs it (same as bambuddy_api.cpp).
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) ptr = malloc(size);
    return ptr;
  }
  void deallocate(void* pointer) override { heap_caps_free(pointer); }
  void* reallocate(void* ptr, size_t new_size) override {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};

// An answer that is not filtered is kept whole in memory: past this size it is
// refused instead of filling the PSRAM with whatever a wrong server sends.
#define KL_ANSWER_MAX  98304

// How much of a refusal ("Invalid API Key", the text of a G-code error) goes
// into the log line.
#define KL_REFUSAL_LOG 120

// The printer a request goes to, and its key ("" or null: none).
struct Target {
  const KlipperPrinterCfg* cfg;
  const char* key;
};

// What goes out and what comes back. body null is a GET. answer null: the body
// of the reply is not read. filter keeps only the part of the reply that is
// asked for (null: all of it).
struct Payload {
  const char* body;
  size_t len;
  JsonDocument* answer;
  JsonDocument* filter;
};

JsonVariantConst resultOf(const JsonDocument& doc) {
  return doc["result"];
}

// At most n - 1 bytes of src, terminated. Written out because snprintf with a
// source array longer than the target is a format-truncation error.
void copyText(char* dst, size_t n, const char* src) {
  size_t i = 0;
  for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
  dst[i] = '\0';
}

// A key goes into a header as it is, so a CR or LF in it would start a header
// line of its own. The store accepts A-Z a-z 0-9 _ - only; this holds for a
// key that came in by another way (a restored backup).
bool keyFitsHeader(const char* key) {
  for (size_t i = 0; key && key[i]; i++) {
    const unsigned char c = static_cast<unsigned char>(key[i]);
    if (c <= 0x20 || c == 0x7f) return false;
  }
  return true;
}

// The text a refused request came with, on one line, for the log.
void refusalText(BackendHttp& http, char* out, size_t n) {
  out[0] = '\0';
  const int size = http.getSize();
  if (size <= 0 || size > 2048) return;
  const String body = http.getString();
  size_t k = 0;
  for (size_t i = 0; i < body.length() && k + 1 < n; i++) {
    const unsigned char c = static_cast<unsigned char>(body[i]);
    out[k++] = (c < 0x20) ? ' ' : static_cast<char>(c);
  }
  out[k] = '\0';
}

// The reply into p.answer: 200 once it parsed, KL_E_PARSE when it did not.
int readAnswer(BackendHttp& http, const Payload& p) {
  if (!p.filter && http.getSize() > KL_ANSWER_MAX) {
    logSDf("KLIPPER: an answer of %d bytes was refused", http.getSize());
    return KL_E_PARSE;
  }
  const DeserializationError err = p.filter
    ? deserializeJson(*p.answer, http.getStream(), DeserializationOption::Filter(*p.filter))
    : deserializeJson(*p.answer, http.getStream());
  if (err) {
    logSDf("KLIPPER: the answer is no JSON (%s)", err.c_str());
    return KL_E_PARSE;
  }
  return 200;
}

// One request: plain http, the key in the header, the limits of KL_*_MS, and
// every answer in the log with its code (a read that worked at verbose level
// only, see the end). The key is not an argument of any log line in this file.
int exchange(const Target& t, const char* verb, const char* path, const Payload& p) {
  char url[KL_HOST_MAX + 96];
  snprintf(url, sizeof(url), "http://%s:%u%s", t.cfg->host, static_cast<unsigned>(t.cfg->port), path);

  char refusal[KL_REFUSAL_LOG + 1] = "";
  int code;
  BackendHttp http;
  if (!http.begin(url)) {
    code = -1;
  } else {
    http.setConnectTimeout(KL_CONNECT_MS);
    http.setTimeout(KL_ANSWER_MS);
    if (t.key && t.key[0]) {
      if (keyFitsHeader(t.key)) {
        http.addHeader("X-Api-Key", t.key);
      } else {
        // The printer will answer 401; this line says why.
        logSDf("KLIPPER: key of %s not sent, it holds control characters", t.cfg->name);
      }
    }
    if (p.body) {
      http.addHeader("Content-Type", "application/json");
      code = http.POST(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(p.body)), p.len);
    } else {
      code = http.GET();
    }
    if (code >= 200 && code < 300) {
      code = p.answer ? readAnswer(http, p) : 200;
    } else if (code > 0) {
      refusalText(http, refusal, sizeof(refusal));
    }
    http.end();
  }
  // Root: one refresh of the AMS view reads a printer three times, and the view
  // refreshes while it is open; a line per read filled the log with answers
  // nobody looks at (review I1).
  // Fix: a read that worked is a verbose line. Everything else stays at the
  // normal level: every POST (it changes or asks for something), a refusal, a
  // reply that did not parse, a connection that failed.
  // NOT-TO-DO: dropping the line of a good read altogether; at verbose level
  // it is the only trace of what the scale asked the printer.
  if (!p.body && code == 200) {
    if (sd_verbose) {
      logSDf("[verbose] KLIPPER: %s http://%s:%u%s -> %d", verb, t.cfg->host,
             static_cast<unsigned>(t.cfg->port), path, code);
    }
  } else {
    logSDf("KLIPPER: %s http://%s:%u%s -> %d %s", verb, t.cfg->host,
           static_cast<unsigned>(t.cfg->port), path, code, refusal);
  }
  return code;
}

int getJson(const Target& t, const char* path, JsonDocument* answer,
            JsonDocument* filter = nullptr) {
  const Payload p = {nullptr, 0, answer, filter};
  return exchange(t, "GET", path, p);
}

// The JSON text of a document in a buffer of its own, from the working memory:
// a script runs to a few kB and does not belong on a stack. Free it with
// moonrakerFree(). Null when there is no memory.
char* jsonText(const JsonDocument& doc, size_t* len) {
  const size_t need = measureJson(doc);
  char* text = static_cast<char*>(moonrakerAlloc(need + 1));
  if (!text) return nullptr;
  *len = serializeJson(doc, text, need + 1);
  return text;
}

int postJson(const Target& t, const char* path, const JsonDocument& body,
             JsonDocument* answer) {
  // Root: a pool that ran out while a body was built leaves a body with
  // something missing, and the printer takes it without complaint: a query
  // with fewer objects reads as a printer with fewer systems, a proxy request
  // without its query asks for the wrong spool (review of Task 3, M5).
  // Fix: here, so that every POST is covered: an overflowed body is not sent.
  // NOT-TO-DO: sending what fits, or checking at each caller: a new caller
  // forgets it.
  if (body.overflowed()) {
    logSD("KLIPPER: a request did not fit in memory, not sent");
    return -1;
  }
  size_t len = 0;
  char* text = jsonText(body, &len);
  if (!text) {
    logSD("KLIPPER: no memory for a request");
    return -1;
  }
  const Payload p = {text, len, answer, nullptr};
  const int code = exchange(t, "POST", path, p);
  moonrakerFree(text);
  return code;
}

// ------------------------------------------------------------
//  The facts
// ------------------------------------------------------------

// What a read of a printer needs besides the stack: about 7.5 kB in all.
struct FactsScratch {
  KlipperObjectNames names;
  KlipperRaw raw;
};

int readObjectNames(const Target& t, KlipperObjectNames* names) {
  SpiRamAllocator alloc;
  JsonDocument filter;
  filter["result"]["objects"] = true;
  JsonDocument answer(&alloc);
  const int code = getJson(t, "/printer/objects/list", &answer, &filter);
  if (code != 200) return code;
  if (!moonrakerParseObjectList(resultOf(answer), names)) {
    logSD("KLIPPER: the printer sent no list of its objects");
    return KL_E_PARSE;
  }
  return 200;
}

int readStatus(const Target& t, const KlipperObjectNames& names, KlipperRaw* raw) {
  SpiRamAllocator alloc;
  JsonDocument body(&alloc);
  moonrakerBuildQuery(names, body);   // an overflowed body is refused by postJson()
  JsonDocument answer(&alloc);
  const int code = postJson(t, "/printer/objects/query", body, &answer);
  if (code != 200) return code;
  const JsonVariantConst status = resultOf(answer)["status"];
  if (!moonrakerParseQuery(status, names, raw)) {
    logSD("KLIPPER: the answer to the query has no status");
    return KL_E_PARSE;
  }
  return 200;
}

// The HTTP code of the request; *spool_id is the active spool on 200, else 0.
int readActiveSpool(const Target& t, int* spool_id) {
  *spool_id = 0;
  SpiRamAllocator alloc;
  JsonDocument filter;
  filter["result"]["spool_id"] = true;
  JsonDocument answer(&alloc);
  const int code = getJson(t, "/server/spoolman/spool_id", &answer, &filter);
  if (code == 200) *spool_id = moonrakerParseActiveSpool(resultOf(answer));
  return code;
}

// ------------------------------------------------------------
//  The connection test
// ------------------------------------------------------------

void probeServerInfo(const Target& t, KlipperProbe* probe) {
  SpiRamAllocator alloc;
  JsonDocument filter;
  filter["result"]["klippy_state"] = true;
  filter["result"]["components"] = true;
  JsonDocument answer(&alloc);
  probe->info_code = getJson(t, "/server/info", &answer, &filter);
  if (probe->info_code != 200) return;
  // A 200 with no Klipper state in it is not a Moonraker's answer: say so
  // with the code of an answer that is none, so that the verdict is "bad
  // answer" and not "Klipper not ready".
  if (!moonrakerParseServerInfo(resultOf(answer), probe->klippy_state,
                                sizeof(probe->klippy_state), &probe->has_spoolman)) {
    probe->info_code = KL_E_PARSE;
  }
}

// Moonraker's own address of its Spoolman, as it is written there.
bool readSpoolmanAddress(const Target& t, char* out, size_t n) {
  SpiRamAllocator alloc;
  JsonDocument filter;
  filter["result"]["config"]["spoolman"]["server"] = true;
  filter["result"]["orig"]["spoolman"]["server"] = true;
  JsonDocument answer(&alloc);
  if (getJson(t, "/server/config", &answer, &filter) != 200) {
    out[0] = '\0';
    return false;
  }
  return moonrakerParseSpoolmanServer(resultOf(answer), out, n);
}

// The newest spool of the Spoolman the printer talks to, asked of Moonraker's
// proxy. This is a POST that carries a GET: nothing is changed. *id is -1 when
// it could not be read.
void newestThroughPrinter(const Target& t, int* id, char* registered, size_t n) {
  *id = -1;
  registered[0] = '\0';
  SpiRamAllocator alloc;
  JsonDocument body(&alloc);
  body["request_method"] = "GET";
  body["path"] = "/v1/spool";
  body["query"] = "limit=1&sort=id:desc";
  body["use_v2_response"] = true;
  JsonDocument answer(&alloc);
  if (postJson(t, "/server/spoolman/proxy", body, &answer) != 200) return;
  // The v2 answer is {"response": ..., "error": null}.
  const JsonVariantConst result = resultOf(answer);
  if (!result["error"].isNull()) return;
  moonrakerParseNewestSpool(result["response"], id, registered, n);
}

// The same question to the scale's own Spoolman, straight from the scale.
void newestOnScale(const char* scale_spoolman, int* id, char* registered, size_t n) {
  *id = -1;
  registered[0] = '\0';
  SpiRamAllocator alloc;
  JsonDocument answer(&alloc);
  if (spoolmanGetJson(scale_spoolman, "/api/v1/spool?limit=1&sort=id:desc", answer,
                      KL_ANSWER_MS) != 200) {
    return;
  }
  moonrakerParseNewestSpool(answer.as<JsonVariantConst>(), id, registered, n);
}

// Whether the printer's Spoolman is the scale's: the address first, and only
// where the two differ (a name against an address) the newest spool of both.
void probeSameSpoolman(const Target& t, const char* scale_spoolman, KlipperProbe* probe) {
  char theirs[128] = "";
  const bool read = readSpoolmanAddress(t, theirs, sizeof(theirs));
  // The printer's address is kept for the verdict's words: normalised, or as
  // Moonraker writes it where the rules refuse it (the rules leave "" then).
  if (read && !klipperSpoolmanAddr(theirs, probe->spoolman, sizeof(probe->spoolman))) {
    copyText(probe->spoolman, sizeof(probe->spoolman), theirs);
  }
  probe->addr_same = read && klipperSameAddr(theirs, scale_spoolman);
  if (probe->addr_same) return;
  int theirs_id = -1;
  int ours_id = -1;
  char theirs_at[40] = "";
  char ours_at[40] = "";
  newestThroughPrinter(t, &theirs_id, theirs_at, sizeof(theirs_at));
  newestOnScale(scale_spoolman, &ours_id, ours_at, sizeof(ours_at));
  probe->newest = klipperSameNewest(theirs_id, theirs_at, ours_id, ours_at);
  logSDf("KLIPPER: Spoolman address differs, newest spool %d against %d -> %d",
         theirs_id, ours_id, static_cast<int>(probe->newest));
}

void probeSpoolman(const Target& t, const char* scale_spoolman, KlipperProbe* probe) {
  {
    SpiRamAllocator alloc;
    JsonDocument filter;
    filter["result"]["spoolman_connected"] = true;
    JsonDocument answer(&alloc);
    probe->status_code = getJson(t, "/server/spoolman/status", &answer, &filter);
    if (probe->status_code == 200) {
      bool connected = false;
      moonrakerParseSpoolmanStatus(resultOf(answer), &connected);
      probe->sm_connected = connected;
    }
  }
  if (probe->status_code != 200 || !probe->sm_connected) return;
  probeSameSpoolman(t, scale_spoolman, probe);
}

}  // namespace

void* moonrakerAlloc(size_t bytes) {
  void* mem = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
  if (!mem) mem = calloc(1, bytes);
  return mem;
}

void moonrakerFree(void* block) {
  heap_caps_free(block);
}

int moonrakerFetchFacts(const KlipperPrinterCfg& c, const char* key, KlipperFacts* out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out));
  FactsScratch* s = static_cast<FactsScratch*>(moonrakerAlloc(sizeof(FactsScratch)));
  if (!s) {
    logSD("KLIPPER: no memory to read a printer");
    return -1;
  }
  const Target t = {&c, key};
  int code = readObjectNames(t, &s->names);
  if (code == 200) code = readStatus(t, s->names, &s->raw);
  if (code == 200) {
    // A printer without [spoolman] refuses this request with a code (404): the
    // read is complete with no active spool. A connection that broke between
    // two requests is no such answer, and a half read is not shown.
    int active = 0;
    const int spool_code = readActiveSpool(t, &active);
    if (spool_code <= 0) {
      code = spool_code;
    } else {
      s->raw.active_spool = active;
    }
  }
  if (code == 200) klipperBuild(s->raw, out);
  moonrakerFree(s);
  return code;
}

int moonrakerRunScript(const KlipperPrinterCfg& c, const char* key, const char* script) {
  if (!script || !script[0]) return -1;
  SpiRamAllocator alloc;
  JsonDocument body(&alloc);
  body["script"] = script;   // an overflowed body is refused by postJson()
  // The commands are worded in klipper_rules.cpp and cleaned by the parser;
  // the log shows them on one line so that a refused or swallowed one can be
  // found in it.
  char shown[161];
  size_t k = 0;
  for (size_t i = 0; script[i] && k + 1 < sizeof(shown); i++) {
    shown[k++] = (script[i] == '\n') ? '|' : script[i];
  }
  shown[k] = '\0';
  logSDf("KLIPPER: script %s", shown);
  const Target t = {&c, key};
  return postJson(t, "/printer/gcode/script", body, nullptr);
}

int moonrakerSetActiveSpool(const KlipperPrinterCfg& c, const char* key, int spool_id) {
  // Root: Moonraker reads a missing spool_id as null and CLEARS the active
  // spool; a 0 or a negative number would be sent as that number.
  // Fix: nothing is sent for a number below 1, and the body always carries it.
  // NOT-TO-DO: using this call to take a spool away. It is for a spool that
  // is going in; the printer's own macros clear it when a tool is unloaded.
  if (spool_id <= 0) return -1;
  SpiRamAllocator alloc;
  JsonDocument body(&alloc);
  body["spool_id"] = spool_id;
  const Target t = {&c, key};
  return postJson(t, "/server/spoolman/spool_id", body, nullptr);
}

int moonrakerProbe(const KlipperPrinterCfg& c, const char* key, const char* scale_spoolman,
                   KlipperProbe* probe, KlipperFacts* facts) {
  if (!probe) return -1;
  *probe = KlipperProbe{};
  if (facts) memset(facts, 0, sizeof(*facts));
  const char* scale = scale_spoolman ? scale_spoolman : "";
  const Target t = {&c, key};

  // Any answer at all shows the printer is there, so the body is not read.
  probe->access_code = getJson(t, "/access/info", nullptr);
  if (probe->access_code <= 0) return probe->access_code;

  probeServerInfo(t, probe);
  const bool ready = probe->info_code == 200 && strcmp(probe->klippy_state, "ready") == 0;
  if (ready) {
    if (probe->has_spoolman) probeSpoolman(t, scale, probe);
    if (facts) moonrakerFetchFacts(c, key, facts);
  }
  logSDf("KLIPPER: test of %s: access %d, info %d, klippy '%s', [spoolman] %d, status %d, "
         "connected %d, same address %d -> verdict %d",
         c.name, probe->access_code, probe->info_code, probe->klippy_state,
         static_cast<int>(probe->has_spoolman), probe->status_code,
         static_cast<int>(probe->sm_connected), static_cast<int>(probe->addr_same),
         static_cast<int>(klipperTestVerdict(*probe)));
  return 200;
}
