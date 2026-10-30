#include "web/web_backup.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <nvs.h>
#include <string.h>
#include <vector>

#include "hardware/sd_logger.h"
#include "web/backup_rules.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char *NS = "spoolscale";

// The largest blob that travels. Every blob this firmware writes is a float,
// four bytes (prefs_store.cpp); the rest is room for whatever comes later.
static const size_t BLOB_MAX = 256;

static const char *typeName(nvs_type_t t) {
  switch (t) {
    case NVS_TYPE_U8:  return "u8";
    case NVS_TYPE_I8:  return "i8";
    case NVS_TYPE_U16: return "u16";
    case NVS_TYPE_I16: return "i16";
    case NVS_TYPE_U32: return "u32";
    case NVS_TYPE_I32: return "i32";
    case NVS_TYPE_U64: return "u64";
    case NVS_TYPE_I64: return "i64";
    case NVS_TYPE_STR: return "str";
    case NVS_TYPE_BLOB: return "blob";
    default:           return nullptr;
  }
}

// The ten types a file may name. Read back through typeName(), so the names
// a file is written with and the names it is read with are the same table.
static const nvs_type_t TYPES[] = {
  NVS_TYPE_U8,  NVS_TYPE_I8,  NVS_TYPE_U16, NVS_TYPE_I16, NVS_TYPE_U32,
  NVS_TYPE_I32, NVS_TYPE_U64, NVS_TYPE_I64, NVS_TYPE_STR, NVS_TYPE_BLOB,
};

// NVS_TYPE_ANY for a name that is none of the ten, or for no name at all.
static nvs_type_t typeFromName(const char *name) {
  if (!name) return NVS_TYPE_ANY;
  for (nvs_type_t t : TYPES) {
    if (!strcmp(name, typeName(t))) return t;
  }
  return NVS_TYPE_ANY;
}

// Every fault of the store or of memory answers alike on both routes, so the
// card can tell it from a refused file and from a shut gate.
static void sendStoreError(WebServer &srv) {
  srv.send(500, "application/json", "{\"ok\":false,\"error\":\"nvs\"}");
}

namespace {
// Appends to a String. ArduinoJson's own writer for a String empties it
// first, and emptying a String frees the buffer reserved for it; this one
// keeps the reservation, so the file is built in the one block measured for
// it. The cast matters: concat(unsigned char) would append the number.
struct BackupOut {
  String &s;
  size_t write(uint8_t c) { return s.concat((char)c) ? 1 : 0; }
  size_t write(const uint8_t *b, size_t n) { return s.concat(b, n) ? n : 0; }
};
}  // namespace

static void sendBackup(WebServer &srv, bool secrets) {
  JsonDocument doc;
  doc["format"] = BACKUP_FORMAT;
  doc["secrets"] = secrets;
  JsonArray items = doc["settings"].to<JsonArray>();

  Preferences p;
  if (!p.begin(NS, true)) {
    logSDf("Web: settings backup refused (namespace not readable)");
    sendStoreError(srv);
    return;
  }
  // A restore is exact, so whatever a file lacks is erased when it is loaded.
  // A setting that travels and cannot be put into the file therefore refuses
  // the file: one that looks whole and is not would cost that setting on the
  // next restore. The first one lost is named in the log.
  char lost[NVS_KEY_NAME_MAX_SIZE] = "";
  const char *lost_why = nullptr;
  nvs_iterator_t it = nullptr;
  esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_ANY, &it);
  while (e == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    const char *t = typeName(info.type);
    const char *why = nullptr;
    bool take = backupKeyTravels(info.key, secrets);
    if (take && !t) why = "unknown type";
    // Floats are stored as four byte blobs; any blob travels as hex. An empty
    // blob stays out and the file is still whole: putBytes() cannot store an
    // empty one, and a restore refuses empty hex, so the device would refuse
    // its own file. getBytes() answers 0 for an empty blob, for one larger
    // than the buffer and for a failed read alike; the stored length tells
    // the empty one from the two that would lose a setting.
    uint8_t buf[BLOB_MAX];
    size_t n = 0;
    if (take && !why && info.type == NVS_TYPE_BLOB) {
      const size_t stored = p.getBytesLength(info.key);
      if (stored == 0) {
        take = false;
      } else if (stored > sizeof(buf)) {
        why = "blob too large";
      } else {
        n = p.getBytes(info.key, buf, sizeof(buf));
        if (n != stored) why = "blob unreadable";
      }
    }
    if (why) {
      take = false;
      if (!lost_why) {
        snprintf(lost, sizeof(lost), "%s", info.key);
        lost_why = why;
      }
    }
    if (take) {
      JsonObject o = items.add<JsonObject>();
      o["k"] = info.key;
      o["t"] = t;
      switch (info.type) {
        case NVS_TYPE_U8:  o["v"] = p.getUChar(info.key); break;
        case NVS_TYPE_I8:  o["v"] = p.getChar(info.key); break;
        case NVS_TYPE_U16: o["v"] = p.getUShort(info.key); break;
        case NVS_TYPE_I16: o["v"] = p.getShort(info.key); break;
        case NVS_TYPE_U32: o["v"] = p.getUInt(info.key); break;
        case NVS_TYPE_I32: o["v"] = p.getInt(info.key); break;
        case NVS_TYPE_U64: o["v"] = p.getULong64(info.key); break;
        case NVS_TYPE_I64: o["v"] = p.getLong64(info.key); break;
        case NVS_TYPE_STR: o["v"] = p.getString(info.key); break;
        case NVS_TYPE_BLOB: {
          char hex[2 * BLOB_MAX + 1];
          if (backupHexEncode(buf, n, hex, sizeof(hex))) o["v"] = hex;
          break;
        }
        default: break;
      }
    }
    e = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  p.end();
  // A walk that stopped before its end leaves settings out just the same.
  if (e != ESP_ERR_NVS_NOT_FOUND) {
    logSDf("Web: settings backup refused (NVS walk stopped: %s)", esp_err_to_name(e));
    sendStoreError(srv);
    return;
  }
  if (lost_why) {
    logSDf("Web: settings backup refused (%s: %s)", lost, lost_why);
    sendStoreError(srv);
    return;
  }
  // Out of memory, ArduinoJson drops what did not fit and says so only here.
  if (doc.overflowed()) {
    logSDf("Web: settings backup refused (no memory for the document)");
    sendStoreError(srv);
    return;
  }
  // Measured first, so that a String that cannot hold the whole file is
  // found before a byte of it is written, and a short one after.
  const size_t len = measureJsonPretty(doc);
  String out;
  BackupOut w{out};
  if (!out.reserve(len) || serializeJsonPretty(doc, w) != len || out.length() != len) {
    logSDf("Web: settings backup refused (no memory for %u bytes)", (unsigned)len);
    sendStoreError(srv);
    return;
  }
  // The name says what the file holds: one with secrets is a key to the
  // network and the backends, and should be recognisable as one.
  srv.sendHeader("Content-Disposition",
                 secrets ? "attachment; filename=\"spoolmanscale-settings-secrets.json\""
                         : "attachment; filename=\"spoolmanscale-settings.json\"");
  srv.send(200, "application/json", out);
  logSDf("Web: settings backup downloaded (%u settings%s)", (unsigned)items.size(),
         secrets ? ", with secrets" : "");
}

// Whether a value from the file can be stored as the type it names, whole.
// is<uint8_t>() and the like check the range too, so a u8 of 300 is refused
// here rather than stored as 44.
static bool valueFits(nvs_type_t t, JsonVariantConst v) {
  switch (t) {
    case NVS_TYPE_U8:  return v.is<uint8_t>();
    case NVS_TYPE_I8:  return v.is<int8_t>();
    case NVS_TYPE_U16: return v.is<uint16_t>();
    case NVS_TYPE_I16: return v.is<int16_t>();
    case NVS_TYPE_U32: return v.is<uint32_t>();
    case NVS_TYPE_I32: return v.is<int32_t>();
    case NVS_TYPE_U64: return v.is<uint64_t>();
    case NVS_TYPE_I64: return v.is<int64_t>();
    case NVS_TYPE_STR: return v.is<const char *>();
    case NVS_TYPE_BLOB: {
      uint8_t buf[BLOB_MAX];
      size_t n = 0;
      return v.is<const char *>() && backupHexDecode(v.as<const char *>(), buf, sizeof(buf), &n);
    }
    default: return false;
  }
}

// One entry of the file: an object with a key NVS can hold, one of the ten
// type names and a value that fits it.
static bool entryOk(JsonVariantConst e) {
  if (!e.is<JsonObjectConst>()) return false;
  const nvs_type_t t = typeFromName(e["t"].as<const char *>());
  return backupKeyValid(e["k"].as<const char *>()) && t != NVS_TYPE_ANY && valueFits(t, e["v"]);
}

// Writes one entry that entryOk() passed. True if it is in flash afterwards.
static bool putValue(Preferences &p, const char *k, nvs_type_t t, JsonVariantConst v) {
  switch (t) {
    case NVS_TYPE_U8:  return p.putUChar(k, v.as<uint8_t>()) > 0;
    case NVS_TYPE_I8:  return p.putChar(k, v.as<int8_t>()) > 0;
    case NVS_TYPE_U16: return p.putUShort(k, v.as<uint16_t>()) > 0;
    case NVS_TYPE_I16: return p.putShort(k, v.as<int16_t>()) > 0;
    case NVS_TYPE_U32: return p.putUInt(k, v.as<uint32_t>()) > 0;
    case NVS_TYPE_I32: return p.putInt(k, v.as<int32_t>()) > 0;
    case NVS_TYPE_U64: return p.putULong64(k, v.as<uint64_t>()) > 0;
    case NVS_TYPE_I64: return p.putLong64(k, v.as<int64_t>()) > 0;
    case NVS_TYPE_STR: {
      // putString() answers with the length it wrote, so an empty text that
      // was stored reads like one that failed. The key was erased before the
      // writing began, so finding it now is what tells the two apart.
      const char *s = v.as<const char *>();
      return p.putString(k, s) > 0 || (!*s && p.isKey(k));
    }
    case NVS_TYPE_BLOB: {
      uint8_t buf[BLOB_MAX];
      size_t n = 0;
      return backupHexDecode(v.as<const char *>(), buf, sizeof(buf), &n) &&
             p.putBytes(k, buf, n) == n;
    }
    default: return false;
  }
}

// The keys a restore replaces: every key of the namespace that travels with
// this file. Collected in full before the first one is erased; the NVS API
// does not promise that an iterator survives entries being erased under it.
// False if the walk stopped before its end, and then nothing may be erased.
static bool travellingKeys(bool secrets, std::vector<String> &out) {
  nvs_iterator_t it = nullptr;
  esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_ANY, &it);
  while (e == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (backupKeyTravels(info.key, secrets)) out.push_back(String(info.key));
    e = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  return e == ESP_ERR_NVS_NOT_FOUND;
}

// Exact, and in an order that lets a bad file change nothing: check the whole
// file, erase what travels, write what the file holds, restart.
static void restoreBackup(WebServer &srv) {
  JsonDocument doc;
  bool good = srv.hasArg("plain") && !deserializeJson(doc, srv.arg("plain")) &&
              (doc["format"] | 0) == BACKUP_FORMAT && doc["settings"].is<JsonArray>();
  JsonArrayConst items = doc["settings"].as<JsonArrayConst>();
  // An empty list is refused like a foreign file: loaded exactly, it would
  // erase every setting and write none.
  good = good && items.size() > 0;
  if (good) {
    for (JsonVariantConst e : items) {
      if (!entryOk(e)) { good = false; break; }
    }
  }
  if (!good) {
    srv.send(400, "application/json", "{\"ok\":false,\"error\":\"not a settings file\"}");
    return;
  }
  // The file says whether it carries secrets. One made without them leaves
  // the device's Wi-Fi login and keys as they are, rather than erasing them
  // and taking the scale off the network it was just restored over.
  const bool secrets = doc["secrets"] | false;

  std::vector<String> doomed;
  Preferences p;
  if (!travellingKeys(secrets, doomed) || !p.begin(NS, false)) {
    sendStoreError(srv);
    return;
  }
  int cleared = 0;
  for (const String &k : doomed) {
    if (p.remove(k.c_str())) cleared++;
  }
  // Skipped is an entry whose key does not travel with this file, a device
  // fact or a secret in a file made without them, left alone on purpose.
  // Failed is one that travels and did not take: erased above, now missing.
  int written = 0, skipped = 0, failed = 0;
  for (JsonVariantConst e : items) {
    const char *k = e["k"].as<const char *>();
    if (!backupKeyTravels(k, secrets)) {
      skipped++;
    } else if (putValue(p, k, typeFromName(e["t"].as<const char *>()), e["v"])) {
      written++;
    } else {
      failed++;
    }
  }
  p.end();
  logSDf("Web: settings restored (%d written, %d skipped, %d failed, %d cleared)", written, skipped,
         failed, cleared);
  String r = "{\"ok\":true,\"written\":";
  r += written;
  r += ",\"skipped\":";
  r += skipped;
  r += "}";
  srv.send(200, "application/json", r);
  // The values are read at boot. The restart comes from here rather than from
  // the page, so that no scale is left running on what it read before.
  delay(400);            // let the response actually leave before the reset
  ESP.restart();
}

void webBackupRoutes(WebServer &srv) {
  // Without secrets the file changes nothing and is read like the settings
  // pages are. With them it is a key to the network and the backends, and a
  // restore rewrites everything: both sit behind the maintenance gate.
  srv.on("/api/backup", HTTP_GET, [&srv]() {
    // On every answer of this route, the refusals too: no cache keeps a copy
    // of a file that may hold the Wi-Fi login and the keys.
    srv.sendHeader("Cache-Control", "no-store");
    const bool secrets = srv.arg("secrets") == "1";
    if (!webRequire(srv, secrets ? GATE_MAINT : GATE_CONFIG, T(STR_W_C_BACKUP))) return;
    sendBackup(srv, secrets);
  });
  srv.on("/api/restore", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_C_BACKUP))) return;
    restoreBackup(srv);
  });
}

String webBackupCard() {
  String h;
  h.reserve(2400);
  h += F("<div class='card wide'><h2>");
  h += T(STR_W_C_BACKUP);
  h += F("</h2><p class='hint'>");
  h += T(STR_W_BACKUP_HINT);
  h += F("</p><div class='field'><label class='check'><span class='switch'>"
         "<input id='bk-sec' type='checkbox'><i></i></span>");
  h += T(STR_W_BACKUP_SECRETS);
  h += F("</label></div><div class='inrow'><button id='bk-dl'>");
  h += T(STR_W_BACKUP_SAVE);
  h += F("</button></div><div class='field' style='margin-top:18px'><label>");
  h += T(STR_W_BACKUP_LOAD);
  h += F("</label><div class='inrow'><input id='bk-f' type='file' accept='.json,application/json'>"
         "<button class='quiet' id='bk-up'>");
  h += T(STR_W_BACKUP_RESTORE);
  h += F("</button><span class='msg' id='bk-s'></span></div></div></div>");
  h += F("<script>");
  h += webShellJsStrings();
  h += F("const BK={done:");
  h += jsStr(T(STR_W_BACKUP_DONE));
  h += F(",bad:");
  h += jsStr(T(STR_W_BACKUP_BAD));
  h += F("};"
         // Fetched rather than followed as a link: a shut gate answers with a
         // sentence, and a link would save that sentence as the backup.
         "$('bk-dl').addEventListener('click',()=>{"
         // Read once: the name has to match what was asked for, even if the
         // box changes while the file is on its way.
         "const s=$('bk-sec').checked;"
         // The window first, before the fetch: inside a backend's frame the
         // file is saved from a window of its own, and a window can only be
         // opened while the click is still being handled (app.js, downloadStart).
         // Every way out below that does not save closes it again.
         "const h=downloadStart('bk-s');if(!h)return;"
         "fetch('/api/backup'+(s?'?secrets=1':''),{cache:'no-store'})"
         ".then(r=>r.ok?r.blob().then(b=>{"
         "downloadFinish(h,b,'spoolmanscale-settings'+(s?'-secrets':'')+'.json');})"
         // A fault on the scale answers with JSON, a shut gate with its own
         // sentence, which says what to switch on.
         ":r.text().then(t=>{downloadAbort(h);let j=null;try{j=JSON.parse(t)}catch(e){}"
         "flash('bk-s',(j||!t)?WS.err:t,true,6000)}))"
         ".catch(()=>{downloadAbort(h);flash('bk-s',WS.err,true,6000)});});"
         "$('bk-up').addEventListener('click',async()=>{"
         "const f=$('bk-f').files[0];if(!f)return;"
         "let t;try{t=await f.text()}catch(e){flash('bk-s',BK.bad,true,6000);return}"
         // Asked before the file goes out: the scale restarts as soon as it
         // has written it, and the wait depends on whether it has a card.
         "const sd=await rtSd();"
         "const r=await post('/api/restore',t);"
         // Only a refused file is "not a backup". Any other JSON answer is a
         // fault on the scale, and a shut gate answers with its own sentence,
         // which says what to switch on.
         "if(!r.ok){flash('bk-s',r.json?(r.json.error==='not a settings file'?BK.bad:WS.err)"
         ":(r.text||WS.err),true,6000);return}"
         "flash('bk-s',BK.done.replace('%d',r.json.written),false,0);"
         "restartWait(sd);});"
         "</script>");
  return h;
}
