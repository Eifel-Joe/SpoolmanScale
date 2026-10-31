#include "services/copy_bbs.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "hardware/sd_logger.h"
#include "services/backend.h"         // backendBaseUrl(), bambuddyApiKey()
#include "services/bambuddy_api.h"
#include "services/copy_rules.h"
#include "services/http_progress.h"   // HttpStallTime
#include "services/spool_types.h"     // spoolTypesActive()

namespace {

// ArduinoJson has to be told to use PSRAM, and every translation unit that
// needs it defines its own allocator (see bambuddy_api.cpp).
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

// One of the three lists a spool carries beside its own fields: the route
// that replaces it, the keys of an entry that Bambuddy takes as input, and
// the bit that reports it as not taken over.
struct LinkList {
  const char*        what;
  const char* const* keys;
  uint8_t            n_keys;
  uint8_t            bit;
};

}  // namespace

// Each read before the card may hold the loop this long; the card waits on it.
#define COPY_BBS_READ_TIMEOUT_MS  3000
// The suppliers the card looks through for the purchase source. Bambuddy
// allows one source per spool, and a spool with more suppliers than this
// shows the first one.
#define COPY_BBS_SUPPLIERS_MAX    32

// The input fields of the three lists, as the instance's OpenAPI names them
// (SpoolFilamentPresetBase, SpoolSupplierLinkInput, SpoolKProfileBase). What
// the answer adds - the link's own id, spool_id, created_at, supplier_name -
// belongs to the template's links and stays behind.
static const char* const PRESET_KEYS[]   = { "printer_model", "nozzle_diameter",
                                             "slicer_filament", "slicer_filament_name" };
static const char* const SUPPLIER_KEYS[] = { "supplier_id", "supplier_article_number",
                                             "quoted_price_per_kg", "is_purchase_source" };
static const char* const KPROFILE_KEYS[] = { "printer_id", "extruder", "nozzle_diameter",
                                             "nozzle_type", "k_value", "name", "cali_idx",
                                             "setting_id" };
#define COPY_BBS_KEYS(a)  ((uint8_t)(sizeof(a) / sizeof((a)[0])))
static const LinkList LIST_PRESETS   = { "filament-presets", PRESET_KEYS,
                                         COPY_BBS_KEYS(PRESET_KEYS),   CM_PRESETS };
static const LinkList LIST_SUPPLIERS = { "suppliers",        SUPPLIER_KEYS,
                                         COPY_BBS_KEYS(SUPPLIER_KEYS), CM_SUPPLIERS };
static const LinkList LIST_KPROFILES = { "k-profiles",       KPROFILE_KEYS,
                                         COPY_BBS_KEYS(KPROFILE_KEYS), CM_KPROFILES };

static SpiRamAllocator s_alloc;
// The template as Bambuddy stores it and its profiles per printer model, from
// copyBbsLoad() to the next load or copyBbsClear().
static JsonDocument s_tpl(&s_alloc);
static JsonDocument s_presets(&s_alloc);
static int     s_loaded_id    = 0;        // the template both belong to, 0: nothing loaded
static int     s_presets_code = 0;        // the presets read: 200, else its failure
static int     s_filament_id  = 0;        // the template's Spoolman filament, 0: not read
static float   s_filament_g   = 0.0f;     // that filament's nominal weight, 0: none or not read
static bool    s_price_typed  = false;    // the price button was used since the load
static float   s_price        = 0.0f;     // the price per kg typed, 0: emptied
static uint8_t s_missing      = CM_NONE;  // the last create's CopyMissing bits

void copyBbsClear() {
  s_tpl.clear();
  s_presets.clear();
  s_loaded_id    = 0;
  s_presets_code = 0;
  s_filament_id  = 0;
  s_filament_g   = 0.0f;
  s_price_typed  = false;
  s_price        = 0.0f;
}

uint8_t copyBbsLastMissing() { return s_missing; }

void copyBbsSetPrice(float typed_price) {
  s_price_typed = true;
  s_price       = typed_price;
}

// The reads of copyBbsLoad() without its log line on failure: the create
// reads through here when nothing was loaded for its template and says it in
// the words it always used. Returns the HTTP code of the template read; with
// anything but 200 nothing is loaded.
static int loadTemplate(int template_spool_id) {
  copyBbsClear();
  const int code = bbGetSpoolRawJson(backendBaseUrl(), bambuddyApiKey(), template_spool_id,
                                     s_tpl, COPY_BBS_READ_TIMEOUT_MS);
  if (code != 200) {
    s_tpl.clear();
    return code;
  }
  // Not part of the spool. A failed read is not "no presets": the card shows
  // no count and the create reports them as not taken over on the status
  // line, where an empty list (200, []) is none and nothing is said.
  s_presets_code = bbGetSpoolPresetsJson(backendBaseUrl(), bambuddyApiKey(), template_spool_id,
                                         s_presets, COPY_BBS_READ_TIMEOUT_MS);
  if (s_presets_code != 200) s_presets.clear();
  // Leaves 0 and logs the cause when Spoolman cannot tell (copy_bbs.h: why).
  if (spoolTypesActive()) {
    bbSpoolmanFilamentOf(template_spool_id, &s_filament_id, COPY_BBS_READ_TIMEOUT_MS, &s_filament_g);
  }
  s_loaded_id = template_spool_id;

  JsonObjectConst tpl = s_tpl.as<JsonObjectConst>();
  const char* profile = tpl["slicer_filament"] | "";
  logSDf("Copy: template %d read, profile %s, %d model presets (HTTP %d), %d suppliers, "
         "%d K profiles, filament %d (%.0f g)",
         template_spool_id, copyRulesHasProfile(profile) ? profile : "none",
         (int)s_presets.as<JsonArrayConst>().size(), s_presets_code,
         (int)tpl["suppliers"].as<JsonArrayConst>().size(),
         (int)tpl["k_profiles"].as<JsonArrayConst>().size(), s_filament_id, s_filament_g);
  return 200;
}

bool copyBbsLoad(int template_spool_id) {
  HttpStallTime stall(__func__);   // the loop stands still for these reads
  const int code = loadTemplate(template_spool_id);
  if (code != 200) logSDf("Copy: template %d unreadable (HTTP %d)", template_spool_id, code);
  return code == 200;
}

float copyBbsNominal(int template_spool_id, float tpl_label_g) {
  // Only a load of this template counts, as for the create (copy_bbs.h); any
  // other is no filament weight: the template's label, as before.
  const bool ours = s_loaded_id > 0 && s_loaded_id == template_spool_id;
  return copyRulesNominal(ours ? s_filament_g : 0.0f, tpl_label_g);
}

// The supplier the card names beside the location.
static const char* supplierShown(JsonArrayConst suppliers) {
  bool source[COPY_BBS_SUPPLIERS_MAX];
  int n = 0;
  for (JsonVariantConst s : suppliers) {
    if (n >= COPY_BBS_SUPPLIERS_MAX) break;
    source[n++] = s["is_purchase_source"] | false;
  }
  const int i = copyRulesSupplierShown(source, n);
  if (i < 0) return "";
  return suppliers[i]["supplier_name"] | "";
}

bool copyBbsView(CopyBbsView* out) {
  if (!out) return false;
  out->profile_name  = "";
  out->model_presets = 0;
  out->location      = "";
  out->supplier      = "";
  out->cost_per_kg   = 0.0f;
  if (s_loaded_id <= 0) return false;

  JsonObjectConst tpl = s_tpl.as<JsonObjectConst>();
  const char* profile = tpl["slicer_filament"] | "";
  if (copyRulesHasProfile(profile)) {
    // The id goes along with the copy, so a profile without a name shows its
    // id rather than the dash that says "nothing taken over".
    const char* name = tpl["slicer_filament_name"] | "";
    out->profile_name = name[0] ? name : profile;
  }
  if (s_presets_code == 200) out->model_presets = (int)s_presets.as<JsonArrayConst>().size();
  // Bambuddy fills the name alongside location_id, so it names the place
  // whichever of the two the create sends.
  out->location    = tpl["storage_location"] | "";
  out->supplier    = supplierShown(tpl["suppliers"].as<JsonArrayConst>());
  // The price per kg as the server keeps it, never effective_cost_per_kg,
  // and the one the create sends while the button is untouched. Only a real
  // price is shown: none, zero or not a number leaves the button its word.
  const float cost = tpl["cost_per_kg"] | 0.0f;
  out->cost_per_kg = (cost > 0.0f && isfinite(cost)) ? cost : 0.0f;
  return true;
}

// The fields a copy behind Bambuddy has always carried, from the template.
// The label weight is the caller's: the template's in Bambuddy's own
// inventory, the start rule's behind Spoolman (spool_start.h, from the
// filament's nominal weight, copyBbsNominal()).
static void commonFields(JsonObjectConst tpl, const CopyBbsCreate& in, int label_g,
                         BbNewSpool& ns) {
  ns.material = tpl["material"] | "";
  ns.subtype  = tpl["subtype"]  | "";
  ns.brand    = tpl["brand"]    | "";
  ns.rgba     = tpl["rgba"]     | "";
  ns.label_weight = label_g;
  // The empty spool (L21): a type chosen for the copy, else the template's
  // own, read with the template; tare_g stands in for a template without
  // core_weight. One whose empty spool is inherited (core_weight_is_inherited,
  // the proxy only) gives none: 0 stays out of the request and the copy
  // inherits as its template does. A type has whole grams, one at least.
  const bool inherited = tpl["core_weight_is_inherited"] | false;
  const int  chosen    = in.chosen_core_g > 0.0f ? (int)lroundf(in.chosen_core_g) : 0;
  ns.core_weight = copyRulesCore(chosen, tpl["core_weight"] | (int)lroundf(in.tare_g), inherited);
  ns.weight_used = (float)label_g - in.remaining_g;
  if (ns.weight_used < 0.0f) ns.weight_used = 0.0f;
}

static int templateUnreadable(int template_spool_id, int code) {
  logSDf("BamBuddy: copy template %d unreadable (HTTP %d), nothing created",
         template_spool_id, code);
  return code;
}

// The copy in Bambuddy's own inventory, as it always was: what a copy takes
// over beyond these fields is for Bambuddy on Spoolman only.
// The template is read back raw at the create, because the mapped form folds
// subtype and color_name into one name and trims rgba to six characters - a
// copy built from it would be poorer than its template. How big from the
// template, how full from the reading.
static int createAsBefore(const CopyBbsCreate& in, int* out_spool_id) {
  JsonDocument tpl(&s_alloc);
  const int code = bbGetSpoolRawJson(backendBaseUrl(), bambuddyApiKey(), in.template_spool_id,
                                     tpl, BB_CREATE_TIMEOUT_MS);
  if (code != 200) return templateUnreadable(in.template_spool_id, code);

  BbNewSpool ns;
  commonFields(tpl.as<JsonObjectConst>(), in, tpl["label_weight"] | (int)lroundf(in.initial_g), ns);
  ns.color_name = tpl["color_name"] | "";
  return bbCreateSpool(backendBaseUrl(), bambuddyApiKey(), ns, out_spool_id,
                       BB_CREATE_TIMEOUT_MS);
}

// One list of the template onto the new spool, complete, and only when the
// template has entries (copy_bbs.h: why). Every entry carries the input keys
// with the template's values, a null as null; a key the server did not send
// stays out. CM_NONE when it went through or there was nothing to send,
// otherwise the list's bit.
static uint8_t putLinks(int spool_id, JsonArrayConst src, const LinkList& list) {
  if (src.size() == 0) return CM_NONE;

  JsonDocument doc(&s_alloc);
  JsonArray out = doc.to<JsonArray>();
  for (JsonVariantConst entry : src) {
    JsonObject o = out.add<JsonObject>();
    for (uint8_t k = 0; k < list.n_keys; k++) {
      JsonVariantConst v = entry[list.keys[k]];
      if (!v.isUnbound()) o[list.keys[k]] = v;
    }
  }
  // The server keeps exactly what is sent, so a list cut short by a full
  // memory would look complete there; better reported as not taken over.
  if (doc.overflowed()) {
    logSDf("Copy: spool %d, %s not taken over (no memory for the list)", spool_id, list.what);
    return list.bit;
  }
  String body;
  serializeJson(doc, body);

  const int code = bbPutSpoolLinks(backendBaseUrl(), bambuddyApiKey(), spool_id, list.what, body);
  if (code == 200) return CM_NONE;
  logSDf("Copy: spool %d, %s not taken over (HTTP %d)", spool_id, list.what, code);
  return list.bit;
}

// What the POST sends behind Spoolman beyond commonFields(); the strings point
// into s_tpl. CM_FILAMENT when the filament id was not read: the copy is made
// without it, and Bambuddy looks one up.
static uint8_t takenOver(JsonObjectConst tpl, BbNewSpool& ns) {
  ns.color_name = copyRulesColorName(tpl["color_name"] | "",
                                     tpl["color_name_is_synthesized"] | false);
  // The price per kg, never effective_cost_per_kg, which Bambuddy may derive:
  // the template's, or the one typed on the card exactly as typed: a price per
  // kg does not change with the label weight. Sent only as a real price: no
  // price, emptied or not a number leaves it out.
  const float cost = copyRulesCostPerKg(tpl["cost_per_kg"] | 0.0f, s_price, s_price_typed);
  if (cost > 0.0f && isfinite(cost)) ns.cost_per_kg = cost;
  // One place, not two: by its id when the template has one, else by name.
  const int location_id = tpl["location_id"] | 0;
  if (location_id > 0) ns.location_id = location_id;
  else ns.storage_location = tpl["storage_location"] | "";
  // Without a profile id the name is one Bambuddy filled in: neither goes.
  const char* profile = tpl["slicer_filament"] | "";
  if (copyRulesHasProfile(profile)) {
    ns.slicer_filament      = profile;
    ns.slicer_filament_name = tpl["slicer_filament_name"] | "";
  }
  if (s_filament_id > 0) {
    ns.spoolman_filament_id = s_filament_id;
    return CM_NONE;
  }
  return CM_FILAMENT;
}

int copyBbsCreate(const CopyBbsCreate& in, int* out_spool_id, uint8_t* out_missing) {
  s_missing = CM_NONE;
  if (out_missing)  *out_missing  = CM_NONE;
  if (out_spool_id) *out_spool_id = 0;
  if (!spoolTypesActive()) return createAsBefore(in, out_spool_id);

  // What the card showed, when it was read for this template (copy_bbs.h).
  if (s_loaded_id <= 0 || s_loaded_id != in.template_spool_id) {
    const int code = loadTemplate(in.template_spool_id);
    if (code != 200) return templateUnreadable(in.template_spool_id, code);
  }

  JsonObjectConst tpl = s_tpl.as<JsonObjectConst>();
  const int label_g = (int)lroundf(in.initial_g);
  BbNewSpool ns;
  commonFields(tpl, in, label_g, ns);
  uint8_t missing = takenOver(tpl, ns);
  logSDf("Copy: template %d, filament %d, profile %s, %.2f per kg (%s), location %d '%s'",
         in.template_spool_id, ns.spoolman_filament_id,
         ns.slicer_filament ? ns.slicer_filament : "none", ns.cost_per_kg,
         s_price_typed ? "typed" : "template", ns.location_id,
         ns.storage_location ? ns.storage_location : "");

  bool warned = false;
  int new_id  = 0;
  const int code = bbCreateSpool(backendBaseUrl(), bambuddyApiKey(), ns, &new_id,
                                 BB_CREATE_TIMEOUT_MS, &warned);
  if (out_spool_id) *out_spool_id = new_id;
  // Nothing to add the lists to; the caller reports the failed create.
  if (code != 200 || new_id <= 0) return code;

  // From here the spool stands, and what fails is reported on the status
  // line, not undone. 207: made, but the price was refused.
  if (warned && ns.cost_per_kg > 0.0f) {
    logSDf("Copy: spool %d, price not taken over (207)", new_id);
    missing |= CM_PRICE;
  }
  if (missing & CM_FILAMENT) {
    logSDf("Copy: spool %d made without its template's filament, Bambuddy chose one", new_id);
  }
  // One list after the other: profiles per model, suppliers, K profiles.
  if (s_presets_code != 200) {
    logSDf("Copy: spool %d, filament-presets not taken over (read HTTP %d)", new_id, s_presets_code);
    missing |= CM_PRESETS;
  } else {
    missing |= putLinks(new_id, s_presets.as<JsonArrayConst>(), LIST_PRESETS);
  }
  missing |= putLinks(new_id, tpl["suppliers"].as<JsonArrayConst>(), LIST_SUPPLIERS);
  missing |= putLinks(new_id, tpl["k_profiles"].as<JsonArrayConst>(), LIST_KPROFILES);

  s_missing = missing;
  if (out_missing) *out_missing = missing;
  return code;
}
