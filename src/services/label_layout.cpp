#include "services/label_layout.h"

#include <stdio.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/prefs_store.h"
#include "services/text_util.h"

// NVS keys, 15 characters at most.
#define LL_KEY_VERSION "lbl_ver"
#define LL_KEY_PRESET  "lbl_preset"
#define LL_KEY_FIELDS  "lbl_fields"
#define LL_KEY_OPTIONS "lbl_opts"
#define LL_KEY_TEXT    "lbl_text"

static const uint32_t FIELD_MASK  = (1u << LABEL_FIELD_COUNT) - 1;
static const uint32_t OPTION_MASK = (1u << LABEL_OPTION_COUNT) - 1;
// What the label printed before the editor: every field up to the brand.
static const uint32_t DEFAULT_FIELDS = (1u << (LF_BRAND + 1)) - 1;

static LabelLayout s_layout{};
static bool s_loaded = false;
static char s_text[LABEL_FREE_TEXT_BYTES] = "";
static bool s_text_loaded = false;

LabelLayout labelLayoutDefault() {
  LabelLayout l{};
  l.preset = LABEL_PRESET_STANDARD;
  l.fields = DEFAULT_FIELDS;
  l.options = 0;
  return l;
}

LabelLayout labelLayoutSanitized(const LabelLayout& in) {
  LabelLayout l = in;
  if (l.preset >= LABEL_PRESET_COUNT) l.preset = LABEL_PRESET_STANDARD;
  l.fields &= FIELD_MASK;
  l.options &= OPTION_MASK;
  return l;
}

bool labelLayoutHas(const LabelLayout& layout, LabelField field) {
  return (layout.fields >> field) & 1u;
}

bool labelLayoutOption(const LabelLayout& layout, LabelOption option) {
  return (layout.options >> option) & 1u;
}

bool labelPresetHasQr(uint8_t preset) {
  return preset != LABEL_PRESET_TEXT_ONLY;
}

bool labelLayoutHasCode(const LabelLayout& layout) {
  return labelLayoutHas(layout, LF_QR) && labelPresetHasQr(layout.preset);
}

// The rolls come 30 mm along the feed, 40 or 50 across. Measured in the
// simulator on 40 x 30 and 50 x 30, flat and upright, with the longest
// lines of the fixture spools: what each arrangement prints without a line
// running into the margin or the code shrinking under its readable size.
#define LL_LONG_ROLL_MM 50
enum CapacityRow { CAP_FLAT_40, CAP_FLAT_50, CAP_UPRIGHT_40, CAP_UPRIGHT_50, CAP_ROWS };
static const uint8_t FACT_CAPACITY[CAP_ROWS][LABEL_PRESET_COUNT] = {
  // standard, compact, big code, material big, text only (never has a code)
  { 5, 6, 6, 3, 5 },   // 40 x 30
  { 5, 6, 6, 3, 5 },   // 50 x 30
  { 3, 4, 2, 2, 6 },   // 30 x 40
  { 5, 6, 4, 3, 6 },   // 30 x 50
};
// The same with the code switched off: the facts get its room, in two
// columns where they fit. Flat, a date line is too wide for half the label,
// so the sixth line has no room there.
static const uint8_t FACT_CAPACITY_NO_CODE[CAP_ROWS][LABEL_PRESET_COUNT] = {
  { 5, 6, 5, 5, 5 },
  { 5, 6, 5, 5, 5 },
  { 6, 6, 6, 6, 6 },
  { 6, 6, 6, 6, 6 },
};

int labelFactCapacity(const LabelLayout& layout, uint16_t width_mm, uint16_t length_mm) {
  const uint8_t preset = layout.preset < LABEL_PRESET_COUNT ? layout.preset : LABEL_PRESET_STANDARD;
  const uint16_t longer = width_mm > length_mm ? width_mm : length_mm;
  const bool long_roll = longer >= LL_LONG_ROLL_MM;
  const CapacityRow row = labelLayoutOption(layout, LO_PORTRAIT)
      ? (long_roll ? CAP_UPRIGHT_50 : CAP_UPRIGHT_40)
      : (long_roll ? CAP_FLAT_50 : CAP_FLAT_40);
  return labelLayoutHasCode(layout) ? FACT_CAPACITY[row][preset]
                                    : FACT_CAPACITY_NO_CODE[row][preset];
}

uint32_t labelFactFields(uint8_t preset) {
  uint32_t m = (1u << LF_SPOOL_ID) | (1u << LF_COLOR) | (1u << LF_DATE) | (1u << LF_BRAND) |
               (1u << LF_ARTICLE) | (1u << LF_SPOOL_WEIGHT) | (1u << LF_NET_WEIGHT) |
               (1u << LF_TEMPS) | (1u << LF_LOCATION) | (1u << LF_DRIED) | (1u << LF_FREE_TEXT);
  // The compact arrangement sets the maker among the facts.
  if (preset == LABEL_PRESET_COMPACT) m |= 1u << LF_VENDOR;
  return m;
}

LabelLayout labelLayoutLoad() {
  if (s_loaded) return s_layout;
  LabelLayout l = labelLayoutDefault();
  // Nothing stored yet: the default, which is the label from before.
  if (prefsGetUInt(LL_KEY_VERSION, 0) != 0) {
    l.preset = (uint8_t)prefsGetUInt(LL_KEY_PRESET, LABEL_PRESET_STANDARD);
    l.fields = prefsGetUInt(LL_KEY_FIELDS, l.fields);
    l.options = prefsGetUInt(LL_KEY_OPTIONS, 0);
  }
  s_layout = labelLayoutSanitized(l);
  s_loaded = true;
  return s_layout;
}

bool labelLayoutSave(const LabelLayout& in) {
  const LabelLayout l = labelLayoutSanitized(in);
  bool ok = true;
  ok = prefsPutUInt(LL_KEY_PRESET, l.preset) && ok;
  ok = prefsPutUInt(LL_KEY_FIELDS, l.fields) && ok;
  ok = prefsPutUInt(LL_KEY_OPTIONS, l.options) && ok;
  // Last, so a version only stands next to a complete set.
  ok = prefsPutUInt(LL_KEY_VERSION, LABEL_LAYOUT_VERSION) && ok;
  if (ok) { s_layout = l; s_loaded = true; }
  logSDf("Label: layout preset %u fields %02x options %02x %s", (unsigned)l.preset,
         (unsigned)l.fields, (unsigned)l.options, ok ? "saved" : "NOT saved");
  return ok;
}

const char* labelLayoutFreeText() {
  if (s_text_loaded) return s_text;
  utf8Cut(prefsGetString(LL_KEY_TEXT, "").c_str(), sizeof(s_text) - 1, s_text, sizeof(s_text));
  s_text_loaded = true;
  return s_text;
}

bool labelLayoutSaveFreeText(const char* text) {
  char cut[LABEL_FREE_TEXT_BYTES];
  utf8Cut(text ? text : "", sizeof(cut) - 1, cut, sizeof(cut));
  const bool ok = prefsPutString(LL_KEY_TEXT, cut);
  if (ok) { snprintf(s_text, sizeof(s_text), "%s", cut); s_text_loaded = true; }
  logSDf("Label: free text of %u bytes %s", (unsigned)strlen(cut), ok ? "saved" : "NOT saved");
  return ok;
}
