#pragma once

#include <stdint.h>

// ============================================================
//  WHAT A SPOOL LABEL SHOWS
//
//  The one template the label editor in the browser sets: an
//  arrangement, which fields go on the label and a few options.
//  The renderer reads it, nothing else decides. The default is
//  the label as it printed before the editor existed, bit for
//  bit, so a scale that never opens the editor prints as before.
//
//  Built to grow: a field or an option is a bit, a new one goes
//  at the end, none is ever renumbered or reused (the bits are
//  NVS content). LABEL_LAYOUT_VERSION is stored alongside, so a
//  later version can tell a mask saved before a field existed
//  from one that switched it off.
// ============================================================

#define LABEL_LAYOUT_VERSION 1

// Where the blocks go. NVS content like the bits below.
enum LabelPreset : uint8_t {
  LABEL_PRESET_STANDARD = 0,   // maker large on top, band, title, facts beside the code
  LABEL_PRESET_COMPACT  = 1,   // no large maker: it joins the facts, the code grows
  LABEL_PRESET_BIG_QR   = 2,   // the code takes the full height, the text a column
  LABEL_PRESET_MATERIAL_BIG = 3, // the material across a third of the label, a small code
  LABEL_PRESET_TEXT_ONLY = 4,  // no code, the facts large, in two columns where they fit
  LABEL_PRESET_COUNT
};

enum LabelField : uint8_t {
  LF_VENDOR = 0, LF_MATERIAL = 1, LF_NAME = 2, LF_SPOOL_ID = 3,
  LF_COLOR = 4, LF_DATE = 5, LF_QR = 6, LF_BRAND = 7,
  // Off by default: the default stays the label from before, and a mask
  // saved before the field existed has the bit clear already.
  LF_ARTICLE = 8,
  // Off by default as well. The location and the drying change while the
  // label stays; the page says so next to them.
  LF_SPOOL_WEIGHT = 9, LF_NET_WEIGHT = 10, LF_TEMPS = 11, LF_LOCATION = 12,
  LF_DRIED = 13, LF_FREE_TEXT = 14,
  // The drying the filament wants, temperature and hours: the backend's
  // value, else the Bambu tag's. Off by default.
  LF_DRYING = 15,
  // The colour as "RGB: 242, 242, 232", a line of its own right under the
  // colour, to type into Bambu Studio. Off by default.
  LF_COLOR_RGB = 16,
  LABEL_FIELD_COUNT
};

enum LabelOption : uint8_t {
  LO_MATERIAL_PLAIN = 0,   // the material as a line, not white on a black band
  LO_DATE_ADDED     = 1,   // the day the spool was added, never the first use
  // Upright: the label drawn 30 wide and 40 or 50 tall, turned onto the roll.
  // The head starts on the left, as on FilaMan's portrait presets.
  LO_PORTRAIT       = 2,
  LABEL_OPTION_COUNT
};

// What a label is for: a spool, or the filament it is made of (for a dryer
// box, a sample card, a shelf). Each has a template of its own.
enum LabelKind : uint8_t {
  LABEL_KIND_SPOOL = 0,
  LABEL_KIND_FILAMENT = 1,
  LABEL_KIND_COUNT
};

struct LabelLayout {
  uint8_t  preset;
  uint32_t fields;    // bit n set: LabelField n is on the label
  uint32_t options;   // bit n set: LabelOption n applies
};

// The spool's: the fields of the label from before on, no option, the
// standard arrangement. The filament's: maker, material, name, colour,
// temperatures and the code to the filament's page.
LabelLayout labelLayoutDefault(LabelKind kind = LABEL_KIND_SPOOL);
// The fields a kind of label has at all: a filament has no spool id, no
// dates, no tare, no place.
uint32_t labelKindFields(LabelKind kind);
// Known bits only, a known arrangement: what the browser sent may be anything.
LabelLayout labelLayoutSanitized(const LabelLayout& in);
// Inline since v0.8.7-beta.7: the pure rules of the label at the scale
// (label_edit_rules.h) and their host tests use them without NVS.
inline bool labelLayoutHas(const LabelLayout& layout, LabelField field) {
  return (layout.fields >> field) & 1u;
}
inline bool labelLayoutOption(const LabelLayout& layout, LabelOption option) {
  return (layout.options >> option) & 1u;
}

// Whether an arrangement draws a code at all; the QR field means nothing
// where it does not.
inline bool labelPresetHasQr(uint8_t preset) {
  return preset != LABEL_PRESET_TEXT_ONLY;
}
// Whether this layout's label carries a code: the arrangement has one and
// the field is on.
inline bool labelLayoutHasCode(const LabelLayout& layout) {
  return labelLayoutHas(layout, LF_QR) && labelPresetHasQr(layout.preset);
}
// How many fact lines this layout keeps whole on a roll of this size, upright
// or not, with its code or without. In millimetres, so it holds at 203 and
// 300 dpi. The renderer stops there, and the browser lets no more be on.
int labelFactCapacity(const LabelLayout& layout, uint16_t width_mm, uint16_t length_mm);

// The most fact lines a label has: every fact a template can tick, the
// maker among them in the compact arrangement (labelFactFields()), which
// continuous tape flat takes all of (Spec V77). What one list holds
// (services/label_facts.h LABEL_MAX_FACTS, held equal there).
#define LABEL_LAYOUT_MAX_FACTS 14

// The shapes the fact counts are kept for (labelFactCapacity()). The Brother's
// rolls of v0.8.7-beta.7 (Spec V75, changed on Joerg's word 05.11.2026, V77
// 06.11.2026): continuous tape (length 0) flat has no budget of its own, the
// label grows with its lines, upright it has its width's; the 23 x 23 square (and anything under 30 mm both ways) carries
// the code alone. A Bluetooth stock kept from earlier betas (75 x 50, 75 x 30)
// keeps the rows it had.
enum LabelRollShape : uint8_t { LRS_40, LRS_50, LRS_62, LRS_ENDLESS, LRS_SMALL };

// Continuous tape: a roll with a width and no length of its own.
inline bool labelMediaEndless(uint16_t width_mm, uint16_t length_mm) {
  return width_mm > 0 && length_mm == 0;
}

inline bool labelMediaSmall(uint16_t width_mm, uint16_t length_mm) {
  return !labelMediaEndless(width_mm, length_mm) && width_mm < 30 && length_mm < 30;
}

inline LabelRollShape labelRollShape(uint16_t width_mm, uint16_t length_mm) {
  if (labelMediaEndless(width_mm, length_mm)) return LRS_ENDLESS;
  if (labelMediaSmall(width_mm, length_mm)) return LRS_SMALL;
  const uint16_t longer = width_mm > length_mm ? width_mm : length_mm;
  const uint16_t shorter = width_mm > length_mm ? length_mm : width_mm;
  if (longer >= 62 && shorter <= 29) return LRS_62;
  return longer >= 50 ? LRS_50 : LRS_40;
}

// The template as a roll of this size carries it: the square takes the code
// alone, in the standard arrangement, flat (Joerg 05.11.2026: "nur fuer
// QR-Code geeignet"); every other roll takes it as it is, continuous tape
// upright too: there the label turns, its height the tape's width and its
// length what it needs (Spec V77, Joerg 06.11.2026: the printer prints a
// label turned as well). Pure: the saved template is untouched.
inline LabelLayout labelLayoutForMedia(const LabelLayout& layout, uint16_t width_mm,
                                       uint16_t length_mm) {
  LabelLayout l = layout;
  const LabelRollShape shape = labelRollShape(width_mm, length_mm);
  if (shape == LRS_SMALL) {
    l.fields &= 1u << LF_QR;
    l.preset = LABEL_PRESET_STANDARD;
  }
  if (shape == LRS_SMALL) l.options &= ~(1u << LO_PORTRAIT);
  return l;
}
// The fields that become a fact line in this arrangement, as a mask: what
// the capacity counts. The header blocks and the code are not among them.
inline uint32_t labelFactFields(uint8_t preset) {
  uint32_t m = (1u << LF_SPOOL_ID) | (1u << LF_COLOR) | (1u << LF_DATE) | (1u << LF_BRAND) |
               (1u << LF_ARTICLE) | (1u << LF_SPOOL_WEIGHT) | (1u << LF_NET_WEIGHT) |
               (1u << LF_TEMPS) | (1u << LF_LOCATION) | (1u << LF_DRIED) | (1u << LF_FREE_TEXT) |
               (1u << LF_DRYING) | (1u << LF_COLOR_RGB);
  // The compact arrangement sets the maker among the facts.
  if (preset == LABEL_PRESET_COMPACT) m |= 1u << LF_VENDOR;
  return m;
}

// Cached after the first read; every write goes through the save.
LabelLayout labelLayoutLoad(LabelKind kind = LABEL_KIND_SPOOL);
bool labelLayoutSave(const LabelLayout& layout, LabelKind kind = LABEL_KIND_SPOOL);

// The free line LF_FREE_TEXT prints, empty when none was set. The buffer
// holds 40 characters of up to 4 bytes each, so a line of umlauts or a euro
// sign keeps all 40 (64 bytes cut a line of umlauts after 31).
#define LABEL_FREE_TEXT_CHARS 40
#define LABEL_FREE_TEXT_BYTES (LABEL_FREE_TEXT_CHARS * 4 + 1)
const char* labelLayoutFreeText();
// Cut to the buffer on a character boundary; false when NVS refused it.
bool labelLayoutSaveFreeText(const char* text);
