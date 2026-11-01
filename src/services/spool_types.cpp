#include "services/spool_types.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_state.h"   // TareSource
#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/backend_http.h"
#include "services/bambuddy_api.h"
#include "services/http_progress.h"
#include "lang.h"   // T(), last: its macro T(id) is a name other headers may use

// The same path in both inventory modes: the catalog is Bambuddy's own table,
// not part of what is proxied to Spoolman (checked against Bambuddy 1.3.0b1 in
// Spoolman mode, 25.10.2026).
#define SPOOL_TYPES_PATH  "/api/v1/inventory/catalog"

namespace {

// ArduinoJson has to be told to use PSRAM, and every translation unit that
// needs it defines its own allocator (see bambuddy_filament_db.cpp).
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

SpiRamAllocator s_psram;

struct CatalogRead {
  JsonDocument* doc;
  JsonDocument* filter;
};

}  // namespace

// The list: one block of SPOOL_TYPES_MAX places, made on the first load and
// kept, so that a pointer from spoolTypesList() stays good.
static SpoolType* s_list  = nullptr;
static int        s_count = 0;

bool spoolTypesActive() {
  return backendMode() == BACKEND_BAMBUDDY && bbInventoryMode() == BB_INV_SPOOLMAN;
}

int spoolTypesCount() { return s_count; }

const SpoolType* spoolTypesList() { return s_list; }

static bool readCatalog(Stream& body, void* ctx) {
  CatalogRead* r = (CatalogRead*)ctx;
  const DeserializationError err =
      deserializeJson(*r->doc, body, DeserializationOption::Filter(*r->filter));
  if (err) logSDf("SpoolTypes: catalog did not parse: %s", err.c_str());
  return !err;
}

int spoolTypesLoad(uint32_t timeout_ms) {
  HttpStallTime stall(__func__);
  if (!spoolTypesActive()) return -1;

  if (!s_list) {
    const size_t bytes = SPOOL_TYPES_MAX * sizeof(SpoolType);
    s_list = (SpoolType*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!s_list) s_list = (SpoolType*)malloc(bytes);
    if (!s_list) {
      logSDf("SpoolTypes: no memory for the list");
      return -1;
    }
  }

  // Name and weight only. The ids are not kept (header), so they are not read.
  JsonDocument filter;
  JsonObject keep = filter.to<JsonArray>().add<JsonObject>();
  keep["name"]   = true;
  keep["weight"] = true;

  JsonDocument doc(&s_psram);
  CatalogRead r = { &doc, &filter };
  const int code = bbGetStreamed(backendBaseUrl(), bambuddyApiKey(), SPOOL_TYPES_PATH,
                                 timeout_ms, readCatalog, &r);
  // Anything but a list is no answer to keep: the list stays as it was.
  if (code == 200 && !doc.is<JsonArray>()) {
    logSDf("SpoolTypes: catalog is not a list");
    return -2;
  }
  if (code != 200) {
    logSDf("SpoolTypes: catalog failed (HTTP %d), %d entries kept", code, s_count);
    return code;
  }

  int n = 0, left_out = 0;
  for (JsonObjectConst c : doc.as<JsonArrayConst>()) {
    const int grams = spoolTypeGrams(c["weight"].as<float>());
    if (grams <= 0) continue;
    if (n >= SPOOL_TYPES_MAX) {
      left_out++;
      continue;
    }
    // The whole name: a catalog name is longer than the 39 bytes a database
    // entry has room for, and the vendor stands at its front.
    spoolTypeTrimName(s_list[n].name, sizeof(s_list[n].name), c["name"] | "");
    if (!s_list[n].name[0]) continue;
    s_list[n].grams = grams;
    n++;
  }
  s_count = n;
  logSDf("SpoolTypes: catalog %d entries (HTTP %d)", n, code);
  if (left_out > 0)
    logSDf("SpoolTypes: %d more than the %d places of the list, left out", left_out, SPOOL_TYPES_MAX);
  return 200;
}

// The POST alone, in a scope of its own: the request lets go of its
// connection when it is destroyed, and the reload that follows can then use it.
static int postType(const char* name, int grams, uint32_t timeout_ms) {
  const char* base = backendBaseUrl();
  if (!base || strlen(base) <= 7) return -1;   // not longer than "http://"

  JsonDocument doc;
  doc["name"]   = name;
  doc["weight"] = grams;
  String body;
  serializeJson(doc, body);

  BackendHttp http;
  if (!http.begin(String(base) + SPOOL_TYPES_PATH)) return -1;
  http.setTimeout(timeout_ms);
  http.addHeader("Content-Type", "application/json");
  const char* key = bambuddyApiKey();
  if (key && key[0]) http.addHeader("X-API-Key", key);

  const int code = http.sendRequest("POST", (uint8_t*)body.c_str(), body.length());
  const bool taken = code >= 200 && code < 300;
  if (taken) {
    logSDf("SpoolTypes: created '%s' %d g (HTTP %d)", name, grams, code);
  } else if (code > 0) {
    // What the server objected to, cut so that the line stays short.
    String why = http.getString();
    if (why.length() > 120) why = why.substring(0, 120);
    logSDf("SpoolTypes: create '%s' %d g failed (HTTP %d) %s", name, grams, code, why.c_str());
  } else {
    logSDf("SpoolTypes: create '%s' %d g failed (transport error %d)", name, grams, code);
  }
  http.end();
  return taken ? 200 : code;
}

int spoolTypesCreate(const char* name, int grams, uint32_t timeout_ms) {
  HttpStallTime stall(__func__);
  if (!spoolTypesActive()) return -1;
  char clean[SPOOL_TYPE_NAME_MAX];
  spoolTypeTrimName(clean, sizeof(clean), name);
  if (!clean[0] || grams <= 0) return -1;

  const int code = postType(clean, grams, timeout_ms);
  // The list is read again, not added to: the server numbers and names the
  // entry, and the next screen should show what the server holds.
  if (code == 200) spoolTypesLoad(timeout_ms);
  return code;
}

const char* spoolTareNote(uint8_t source) {
  switch (source) {
    case TARE_FILAMENT:
      return T(spoolTypesActive() ? STR_TARE_INHERITED : STR_TARE_FROM_FILAMENT);
    case TARE_VENDOR:
      return T(STR_TARE_FROM_BRAND);
    default:
      return "";
  }
}
