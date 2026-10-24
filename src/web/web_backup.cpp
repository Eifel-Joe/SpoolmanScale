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

static void sendBackup(WebServer &srv, bool secrets) {
  JsonDocument doc;
  doc["format"] = BACKUP_FORMAT;
  doc["secrets"] = secrets;
  JsonArray items = doc["settings"].to<JsonArray>();

  Preferences p;
  if (!p.begin(NS, true)) {
    srv.send(500, "application/json", "{\"error\":\"nvs\"}");
    return;
  }
  nvs_iterator_t it = nullptr;
  esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NS, NVS_TYPE_ANY, &it);
  while (e == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    const char *t = typeName(info.type);
    bool take = t && backupKeyTravels(info.key, secrets);
    // Floats are stored as four byte blobs; any blob travels as hex. A blob
    // reads as 0 bytes when it is empty or larger than the buffer. Neither
    // goes into the file: putBytes() cannot store an empty one, and a restore
    // refuses empty hex, so the device would refuse its own file.
    uint8_t buf[BLOB_MAX];
    size_t n = 0;
    if (take && info.type == NVS_TYPE_BLOB) {
      n = p.getBytes(info.key, buf, sizeof(buf));
      take = n > 0;
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
  // A walk that stopped before its end would leave settings out of the file,
  // and a restore is exact: loading that file would erase them.
  if (e != ESP_ERR_NVS_NOT_FOUND) {
    srv.send(500, "application/json", "{\"error\":\"nvs\"}");
    return;
  }

  String out;
  serializeJsonPretty(doc, out);
  srv.sendHeader("Content-Disposition", "attachment; filename=\"spoolmanscale-settings.json\"");
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
    srv.send(500, "application/json", "{\"ok\":false,\"error\":\"nvs\"}");
    return;
  }
  int cleared = 0;
  for (const String &k : doomed) {
    if (p.remove(k.c_str())) cleared++;
  }
  int written = 0, skipped = 0;
  for (JsonVariantConst e : items) {
    const char *k = e["k"].as<const char *>();
    if (backupKeyTravels(k, secrets) && putValue(p, k, typeFromName(e["t"].as<const char *>()), e["v"])) {
      written++;
    } else {
      skipped++;
    }
  }
  p.end();
  logSDf("Web: settings restored (%d written, %d skipped, %d cleared)", written, skipped, cleared);
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
         "fetch('/api/backup'+($('bk-sec').checked?'?secrets=1':''),{cache:'no-store'})"
         ".then(r=>r.ok?r.blob().then(b=>{"
         "const a=document.createElement('a');a.href=URL.createObjectURL(b);"
         "a.download='spoolmanscale-settings.json';"
         "document.body.appendChild(a);a.click();a.remove();"
         // Not at once: a browser may still be reading the blob for the save.
         "setTimeout(()=>URL.revokeObjectURL(a.href),10000);})"
         ":r.text().then(t=>flash('bk-s',t||WS.err,true,6000)))"
         ".catch(()=>flash('bk-s',WS.err,true,6000));});"
         "$('bk-up').addEventListener('click',async()=>{"
         "const f=$('bk-f').files[0];if(!f)return;"
         "let t;try{t=await f.text()}catch(e){flash('bk-s',BK.bad,true,6000);return}"
         // Asked before the file goes out: the scale restarts as soon as it
         // has written it, and the wait depends on whether it has a card.
         "const sd=await rtSd();"
         "const r=await post('/api/restore',t);"
         // A refused file answers with JSON, a shut gate with its own
         // sentence, which says what to switch on.
         "if(!r.ok){flash('bk-s',r.json&&r.json.error?BK.bad:(r.text||WS.err),true,6000);return}"
         "flash('bk-s',BK.done.replace('%d',r.json.written),false,0);"
         "restartWait(sd);});"
         "</script>");
  return h;
}
