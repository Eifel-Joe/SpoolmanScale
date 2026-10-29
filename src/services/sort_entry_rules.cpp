#include "services/sort_entry_rules.h"

#include <stdio.h>
#include <string.h>

#include "services/text_fold.h"
#include "services/text_util.h"

// ---- the usual values ------------------------------------------------

// The colours of the grid, in the spelling of SpoolmanDB (plan, Festlegung 2).
// "Transparent" is the clear one and has no hue.
const SortColour SORT_COMMON[SORT_COMMON_COUNT] = {
  {"Black", "000000"}, {"White", "FFFFFF"}, {"Gray", "808080"}, {"Transparent", ""},
  {"Red", "FF0000"}, {"Dark Red", "8B0000"}, {"Orange", "FF8000"}, {"Yellow", "FFFF00"},
  {"Gold", "FFD700"}, {"Beige", "F5F5DC"}, {"Brown", "8B4513"}, {"Light Green", "90EE90"},
  {"Green", "00A000"}, {"Dark Green", "006400"}, {"Olive Green", "808000"}, {"Mint", "98FF98"},
  {"Turquoise", "40E0D0"}, {"Cyan", "00FFFF"}, {"Light Blue", "ADD8E6"}, {"Sky Blue", "87CEEB"},
  {"Blue", "0000FF"}, {"Navy Blue", "000080"}, {"Purple", "800080"}, {"Violet", "EE82EE"},
  {"Magenta", "FF00FF"}, {"Pink", "FFC0CB"}, {"Silver", "C0C0C0"}, {"Dark Gray", "404040"},
};

// What a maker's name for a filament starts with when the database has no
// field for the line (plan, Festlegung 1).
const char* const SORT_LINES[SORT_LINES_COUNT] = {
  "Basic", "Matte", "Silk", "Plus", "Pro", "HF", "High Speed", "Tough", "Lite",
  "CF", "GF", "Glow", "Marble", "Sparkle", "Galaxy", "Wood", "Metal", "Translucent",
};

const uint16_t SORT_SIZES[SORT_SIZES_COUNT] = {1000, 850, 750, 500, 250};

// For a maker the database does not know, the materials to offer.
const char* const SORT_MATERIALS[SORT_MATERIALS_COUNT] = {
  "PLA", "PLA+", "PETG", "ABS", "ASA", "TPU", "PA", "PC", "PCTG", "HIPS", "PVA", "PP",
};

// ---- reading text ------------------------------------------------------

// What sortEntrySame() leaves out of a name.
static bool skipped(char c) {
  return c == ' ' || c == '-';
}

// Whether anything is left of s once the blanks and hyphens are gone.
static bool hasText(const char* s) {
  if (!s) return false;
  for (; *s; s++) {
    if (!skipped(*s)) return true;
  }
  return false;
}

static const char* skipBlanks(const char* s) {
  while (s && *s == ' ') s++;
  return s;
}

// Past a word of len bytes and the blanks and hyphens that follow it.
static const char* afterWord(const char* s, size_t len) {
  s += len;
  while (*s == ' ' || *s == '-') s++;
  return s;
}

// Whether text begins with word as a whole word, in any case: "Tough Cyan"
// begins with "Tough", "Tough+ Cyan" does not. The word ends at the end of the
// text, at a blank or at a dash. The same reading as tagCreateStartsWithWord(),
// which lives in a file that is not host-testable. Both are read a character
// at a time through textFoldNext() (V43), like sortEntrySame(). When after is
// not null it is set to the end of the word in text: the word and the text it
// matched can differ in length (a broken byte meets a sequence of the same
// value), and the caller must step by the text's length, not the word's.
static bool startsWithWord(const char* text, const char* word, const char** after) {
  if (!text || !word || !word[0]) return false;
  const char* t = text;
  const char* w = word;
  while (*w) {
    if (!*t) return false;
    uint32_t ct, cw;
    t = textFoldNext(t, &ct);
    w = textFoldNext(w, &cw);
    if (ct != cw) return false;
  }
  if (*t != '\0' && *t != ' ' && *t != '-') return false;
  if (after) *after = t;
  return true;
}

// The usual line that text begins with, as spelled in SORT_LINES. Where two
// of them fit (one a word of the other) the longer one wins.
static const char* leadingLine(const char* text) {
  if (!text) return nullptr;
  const char* best = nullptr;
  size_t best_len = 0;
  for (int i = 0; i < SORT_LINES_COUNT; i++) {
    const size_t n = strlen(SORT_LINES[i]);
    if (n > best_len && startsWithWord(text, SORT_LINES[i], nullptr)) {
      best = SORT_LINES[i];
      best_len = n;
    }
  }
  return best;
}

// The value of a hexadecimal digit, -1 for anything else.
static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Characters, not bytes, are compared (textFoldNext(), V43). The blanks and
// hyphens are skipped before each character is read: a byte below 0x80 never
// sits inside a UTF-8 sequence, so a skip cannot cut one.
bool sortEntrySame(const char* a, const char* b) {
  if (!a || !b) return false;
  bool any = false;   // at least one character was compared: nothing left is never equal
  for (;;) {
    while (skipped(*a)) a++;
    while (skipped(*b)) b++;
    if (!*a || !*b) return any && !*a && !*b;
    uint32_t ca, cb;
    a = textFoldNext(a, &ca);
    b = textFoldNext(b, &cb);
    if (ca != cb) return false;
    any = true;
  }
}

bool sortEntryHex(const char* text, char out[7]) {
  if (!text) return false;
  if (*text == '#') text++;
  char digits[7] = "";
  int n = 0;
  for (; text[n]; n++) {
    if (n >= 6 || hexValue(text[n]) < 0) return false;
    const char c = text[n];
    digits[n] = (c >= 'a' && c <= 'f') ? (char)(c - ('a' - 'A')) : c;
  }
  if (n != 6) return false;
  // Copied last, so that text and out may be the same array.
  if (out) {
    memcpy(out, digits, 6);
    out[6] = '\0';
  }
  return true;
}

// ---- the lists ---------------------------------------------------------

int sortChoicesFind(const SortChoices& list, const char* name) {
  if (!name) return -1;
  for (int i = 0; i < list.n && i < SORT_CHOICES_MAX; i++) {
    if (sortEntrySame(list.item[i].name, name)) return i;
  }
  return -1;
}

// Appends one entry at the end: the caller has made sure it is new. The name
// and the hue are cut to their fields, the source row is none. Returns the
// index, -1 for a full list.
static int appendChoice(SortChoices* list, const char* name, const char* hex, int grams, uint8_t from) {
  if (list->n < 0 || list->n >= SORT_CHOICES_MAX) return -1;
  SortChoice* c = &list->item[list->n];
  memset(c, 0, sizeof(*c));
  utf8Cut(name, sizeof(c->name) - 1, c->name, sizeof(c->name));
  // A colour is kept as capitals without '#'. Anything else is kept as it is
  // (cut), which sortEntryToInput() refuses later; it must not become an
  // empty hex, which in a colour list means clear.
  if (hex && !sortEntryHex(hex, c->hex)) utf8Cut(hex, sizeof(c->hex) - 1, c->hex, sizeof(c->hex));
  c->grams = (uint16_t)grams;
  c->from = from;
  c->row = -1;
  return list->n++;
}

int sortChoicesAdd(SortChoices* list, const char* name, const char* hex, uint8_t from) {
  if (!list || !name) return -1;
  // Cut first: the list holds the cut name, and the same long name offered
  // twice has to meet itself.
  char cut[SORT_NAME_MAX];
  utf8Cut(name, sizeof(cut) - 1, cut, sizeof(cut));
  if (!hasText(cut)) return -1;
  const int at = sortChoicesFind(*list, cut);
  if (at >= 0) return at;   // the first spelling stays, a full list too
  return appendChoice(list, cut, hex, 0, from);
}

int sortChoicesAddGrams(SortChoices* list, int grams, uint8_t from) {
  if (!list || grams < SORT_GRAMS_MIN || grams > SORT_GRAMS_MAX) return -1;
  for (int i = 0; i < list->n && i < SORT_CHOICES_MAX; i++) {
    if (list->item[i].grams == grams) return i;
  }
  char name[SORT_NAME_MAX];
  snprintf(name, sizeof(name), "%d g", grams);
  return appendChoice(list, name, nullptr, grams, from);
}

// ---- lines and colours in names ----------------------------------------

const char* sortEntryLineOf(const char* name) {
  return leadingLine(skipBlanks(name));
}

bool sortEntryColourOf(const char* name, const char* own_line, const char* chosen_line, char* out, size_t out_size) {
  if (!out || !out_size) return false;
  out[0] = '\0';
  if (!name) return false;
  const char* line = "";
  const char* colour = name;
  if (own_line) {
    line = own_line;   // the entry has its line apart, the name is the colour
  } else {
    const char* start = skipBlanks(name);
    const char* found = leadingLine(start);
    if (found) {
      line = found;
      colour = afterWord(start, strlen(found));
    }
  }
  if (!chosen_line) chosen_line = "";
  // Both without a line belong together; sortEntrySame() alone would call
  // two empty names different.
  const bool same = (!hasText(line) && !hasText(chosen_line)) || sortEntrySame(line, chosen_line);
  if (!same) return false;
  utf8Cut(colour, out_size - 1, out, out_size);
  return true;
}

void sortEntrySplitName(const char* name, const char* material, char line[SORT_LINE_MAX], char colour[SORT_NAME_MAX]) {
  if (line) line[0] = '\0';
  if (colour) colour[0] = '\0';
  if (!name || !line || !colour) return;
  const char* rest = skipBlanks(name);
  // "PETG Basic" names the material in front, which is no part of the line.
  const char* after = nullptr;
  if (material && material[0] && startsWithWord(rest, material, &after)) rest = afterWord(rest, (size_t)(after - rest));
  const char* found = leadingLine(rest);
  if (found) {
    utf8Cut(found, SORT_LINE_MAX - 1, line, SORT_LINE_MAX);
    rest = afterWord(rest, strlen(found));
  }
  utf8Cut(rest, SORT_NAME_MAX - 1, colour, SORT_NAME_MAX);
}

// ---- a Spoolman extra --------------------------------------------------

// The text of an extra is decoded into this scratch first and cut into the
// caller's buffer by utf8Cut() afterwards. It holds more than any name of the
// lists (two fields' worth); what does not fit is dropped.
struct ExtraScratch {
  char   text[2 * SORT_NAME_MAX];
  size_t n;       // bytes in text
  bool   full;    // something did not fit: nothing more is taken
};

// Appends len bytes, whole or not at all (a UTF-8 sequence made of an escape
// is never split here); the last byte of text stays free for the terminator.
static void extraPut(ExtraScratch* x, const char* bytes, size_t len) {
  if (x->full) return;
  if (x->n + len >= sizeof(x->text)) {
    x->full = true;
    return;
  }
  memcpy(x->text + x->n, bytes, len);
  x->n += len;
}

// The UTF-8 bytes of a code point of the basic plane, 1 to 3 of them; returns
// how many. A surrogate half (D800 to DFFF) and 0 are no character of their
// own and give '?'.
static size_t utf8Of(unsigned cp, char out[3]) {
  if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) {
    out[0] = '?';
    return 1;
  }
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  out[0] = (char)(0xE0 | (cp >> 12));
  out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[2] = (char)(0x80 | (cp & 0x3F));
  return 3;
}

bool sortEntryExtraText(const char* raw, char* out, size_t out_size) {
  if (!out || !out_size) return false;
  out[0] = '\0';
  if (!raw) return false;
  const char* p = raw;
  while (*p == ' ') p++;
  if (*p != '"') return false;   // not a JSON string: none, a number, null
  p++;
  ExtraScratch x;
  x.n = 0;
  x.full = false;
  bool closed = false;
  for (; *p; p++) {
    char c = *p;
    if (c == '"') {
      closed = true;
      break;
    }
    if (c == '\\' && p[1] == 'u') {
      // A backslash, u and exactly four hex digits, or the text is not usable.
      unsigned cp = 0;
      for (int k = 0; k < 4; k++) {
        const int v = hexValue(p[2 + k]);   // stops at a terminator: no read past it
        if (v < 0) return false;
        cp = (cp << 4) | (unsigned)v;
      }
      char bytes[3];
      const size_t len = utf8Of(cp, bytes);
      extraPut(&x, bytes, len);
      p += 5;   // the 'u' and the four digits; the loop steps past the last one
      continue;
    }
    // The escapes of a quote, a backslash and a slash are resolved; any other
    // stays as it was written, so that nothing is lost.
    if (c == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '/')) {
      p++;
      c = *p;
    }
    extraPut(&x, &c, 1);
  }
  if (!closed) return false;   // cut off on the way: half a text is worse than none
  x.text[x.n] = '\0';
  // The cut never goes beyond the scratch's last but one byte, so that when
  // the scratch was full the byte after the cut is there for utf8Cut() to see
  // (it backs off from a sequence it would split by looking at that byte).
  size_t limit = out_size - 1;
  if (limit > sizeof(x.text) - 2) limit = sizeof(x.text) - 2;
  utf8Cut(x.text, limit, out, out_size);
  return out[0] != '\0';
}

// ---- the input of the card ---------------------------------------------

bool sortEntryToInput(const SortEntry& e, TagCreateInput* in) {
  if (!in) return false;
  if (!hasText(e.vendor) || !hasText(e.material) || !hasText(e.colour)) return false;
  // A clear filament has no hue, whatever the hex field holds.
  char hex[7] = "";
  if (!e.clear && !sortEntryHex(e.hex, hex)) return false;
  if (e.grams < SORT_GRAMS_MIN || e.grams > SORT_GRAMS_MAX) return false;

  // Checked: from here on the input is rebuilt, nothing of the old stays.
  memset(in, 0, sizeof(*in));
  utf8Cut(e.vendor, sizeof(in->vendor) - 1, in->vendor, sizeof(in->vendor));
  utf8Cut(e.material, sizeof(in->material) - 1, in->material, sizeof(in->material));
  // A line of blanks is no line.
  if (hasText(e.line)) utf8Cut(e.line, sizeof(in->subtype) - 1, in->subtype, sizeof(in->subtype));
  // The product is the material, then the line: "ABS Matte". Both fields in
  // full fit the buffer, so the only cut is the one into the product's field.
  char product[sizeof(in->material) + sizeof(in->subtype) + 1];
  if (in->subtype[0]) snprintf(product, sizeof(product), "%s %s", in->material, in->subtype);
  else                snprintf(product, sizeof(product), "%s", in->material);
  utf8Cut(product, sizeof(in->product) - 1, in->product, sizeof(in->product));
  utf8Cut(e.colour, sizeof(in->color_name) - 1, in->color_name, sizeof(in->color_name));
  utf8Cut(e.colour, sizeof(in->color_name_en) - 1, in->color_name_en, sizeof(in->color_name_en));

  if (e.clear) {
    in->clear = true;
    in->color_count = 0;
    memcpy(in->rgba, "00000000", 9);
  } else {
    memcpy(in->color_hex, hex, sizeof(hex));
    memcpy(in->colors_hex[0], hex, sizeof(hex));
    in->color_count = 1;
    snprintf(in->rgba, sizeof(in->rgba), "%sFF", hex);
  }
  in->color_kind = TCK_SINGLE;
  in->net_weight_g = e.grams;
  // A diameter is 1.75 mm or 2.85 mm; what is not near the second is the first.
  in->diameter_mm = (e.diameter_mm > 2.80f && e.diameter_mm < 2.90f) ? 2.85f : TAG_CREATE_DIAMETER_MM;
  in->names_known = true;
  in->typed = true;
  in->known_filament_id = e.filament_id > 0 ? e.filament_id : 0;
  return true;
}
