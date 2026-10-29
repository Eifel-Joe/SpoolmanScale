#include "services/key_layout.h"

#include <stddef.h>

// ============================================================
//  THE PAGES
//
//  Widths as LVGL's own keyboard has them, in units of a row: row 1 the page
//  key 5, the letters 4, delete 7; row 2 the page key 6, the letters 3, new
//  line 7; row 3 every key 1; the bottom row keyboard key 2, left 2, space 6,
//  right 2, OK 2. A row with one letter more than LVGL's (QWERTZ, AZERTY)
//  keeps the widths of its keys, so the letters only get a little narrower.
// ============================================================

// The flags, three letters each, so that a row of flags lines up with the
// row of widths above it.
static constexpr uint8_t LET = KF_LETTER;
static constexpr uint8_t CHK = KF_CHECKED;
static constexpr uint8_t CTL = KF_CTRL;
static constexpr uint8_t SPC = KF_PLAIN;

// The most keys in a row: the sign row of the WiFi password has 14.
// test_key_layout.py holds every row to it as well.
#define KEY_ROW_MAX  14

// ---- The bottom rows ----

// The text keyboards: the keyboard key (LVGL's CANCEL, the way back), the
// arrows, the space bar and OK, as LVGL has them.
#define ROW4_TEXT    KEY_SYM_KEYBOARD, KEY_SYM_LEFT, " ", KEY_SYM_RIGHT, KEY_SYM_OK, ""
#define ROW4_TEXT_W  2, 2, 6, 2, 2
#define ROW4_TEXT_F  CTL, CHK, SPC, CHK, CTL
// French: the key of the accent page after the keyboard key; the space bar
// gives up the room for it.
#define ROW4_FR      KEY_SYM_KEYBOARD, KEY_ACCENTS, KEY_SYM_LEFT, " ", KEY_SYM_RIGHT, KEY_SYM_OK, ""
#define ROW4_FR_W    2, 2, 2, 4, 2, 2
#define ROW4_FR_F    CTL, CTL, CHK, SPC, CHK, CTL
// The WiFi password: no keyboard key, OK has its width (see the WiFi pages).
#define ROW4_WIFI    KEY_SYM_LEFT, " ", KEY_SYM_RIGHT, KEY_SYM_OK, ""
#define ROW4_WIFI_W  2, 6, 2, 4
#define ROW4_WIFI_F  CHK, SPC, CHK, CTL

// ---- English: LVGL's default_kb_map_lc/uc/spec (lv_keyboard.c 8.3.11,
// lines 49 to 86), the WiFi password's letters as well ----

#define EN_LOWER_ROWS \
  "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", KEY_SYM_BACKSPACE, "\n", \
  "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", KEY_SYM_NEW_LINE, "\n", \
  "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n"
#define EN_UPPER_ROWS \
  "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", KEY_SYM_BACKSPACE, "\n", \
  "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", KEY_SYM_NEW_LINE, "\n", \
  "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n"
// Lower and upper case share their widths and flags; the German WiFi letters
// too, which only swap z and y.
#define EN_ROWS_W \
  5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7, \
  6, 3, 3, 3, 3, 3, 3, 3, 3, 3, 7, \
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
#define EN_ROWS_F \
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK, \
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK, \
  CHK, CHK, LET, LET, LET, LET, LET, LET, LET, CHK, CHK, CHK

static constexpr const char* EN_LOWER[] = { EN_LOWER_ROWS, ROW4_TEXT };
static constexpr const char* EN_UPPER[] = { EN_UPPER_ROWS, ROW4_TEXT };
static constexpr uint8_t EN_TEXT_W[] = { EN_ROWS_W, ROW4_TEXT_W };
static constexpr uint8_t EN_TEXT_F[] = { EN_ROWS_F, ROW4_TEXT_F };

// The page of digits and signs, for every language.
static constexpr const char* SPECIAL[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", KEY_SYM_BACKSPACE, "\n",
  "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
  "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
  ROW4_TEXT
};
static constexpr uint8_t SPECIAL_W[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
  2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  ROW4_TEXT_W
};
static constexpr uint8_t SPECIAL_F[] = {
  LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET,
  LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET,
  ROW4_TEXT_F
};

// ---- German, QWERTZ as on a PC: u umlaut after p, o and a umlaut after l,
// the sharp s after m on both pages (the fonts have no capital sharp s,
// U+1E9E is not in the range of montserrat_ext_*). The special page is the
// English one. ----

static constexpr const char* DE_LOWER[] = {
  "1#", "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü", KEY_SYM_BACKSPACE, "\n",
  "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä", KEY_SYM_NEW_LINE, "\n",
  "_", "-", "y", "x", "c", "v", "b", "n", "m", "ß", ".", ",", ":", "\n",
  ROW4_TEXT
};
static constexpr const char* DE_UPPER[] = {
  "1#", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü", KEY_SYM_BACKSPACE, "\n",
  "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä", KEY_SYM_NEW_LINE, "\n",
  "_", "-", "Y", "X", "C", "V", "B", "N", "M", "ß", ".", ",", ":", "\n",
  ROW4_TEXT
};
static constexpr uint8_t DE_TEXT_W[] = {
  5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7,
  6, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 7,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  ROW4_TEXT_W
};
static constexpr uint8_t DE_TEXT_F[] = {
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CHK, CHK, LET, LET, LET, LET, LET, LET, LET, LET, CHK, CHK, CHK,
  ROW4_TEXT_F
};

// ---- French, AZERTY: m at the end of row 2, the four accented letters
// typed most in row 3 (no underscore and no colon there: the room went to
// them), the others on a page of their own behind the accent key. The
// special page is the English one. ----

static constexpr const char* FR_LOWER[] = {
  "1#", "a", "z", "e", "r", "t", "y", "u", "i", "o", "p", KEY_SYM_BACKSPACE, "\n",
  "ABC", "q", "s", "d", "f", "g", "h", "j", "k", "l", "m", KEY_SYM_NEW_LINE, "\n",
  "-", "w", "x", "c", "v", "b", "n", "é", "è", "à", "ç", ",", ".", "\n",
  ROW4_FR
};
static constexpr const char* FR_UPPER[] = {
  "1#", "A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P", KEY_SYM_BACKSPACE, "\n",
  "abc", "Q", "S", "D", "F", "G", "H", "J", "K", "L", "M", KEY_SYM_NEW_LINE, "\n",
  "-", "W", "X", "C", "V", "B", "N", "É", "È", "À", "Ç", ",", ".", "\n",
  ROW4_FR
};
static constexpr uint8_t FR_TEXT_W[] = {
  5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7,
  6, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 7,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  ROW4_FR_W
};
static constexpr uint8_t FR_TEXT_F[] = {
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CHK, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK, CHK,
  ROW4_FR_F
};
// The accent page (LV_KEYBOARD_MODE_USER_1): small letters in row 1, the
// capitals in row 2, quotes and the signs the letter pages left out in
// row 3. "abc" leads back.
static constexpr const char* FR_ACCENTS[] = {
  "â", "ê", "î", "ô", "û", "ë", "ï", "ü", "ù", "œ", KEY_SYM_BACKSPACE, "\n",
  "abc", "Â", "Ê", "Î", "Ô", "Û", "Ë", "Ï", "Ü", "Ù", "Œ", KEY_SYM_NEW_LINE, "\n",
  "_", ":", "'", "«", "»", "\n",
  ROW4_FR
};
static constexpr uint8_t FR_ACCENTS_W[] = {
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7,
  6, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 7,
  1, 1, 1, 1, 1,
  ROW4_FR_W
};
static constexpr uint8_t FR_ACCENTS_F[] = {
  LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CHK, CHK, CHK, CHK, CHK,
  ROW4_FR_F
};

// ---- The WiFi password (wifi_pass_screen.cpp) ----
//
// LVGL's special page has no ^ ~ | or `, so a password containing one could
// not be typed at all: this sign page has them. The keyboard key at the
// bottom left only sends CANCEL, which nothing on that screen answers, so it
// is gone on all three pages and OK has its width (4 instead of 2, drawn in
// the accent by wifi_pass_screen.cpp). The letters keep the order of the
// language, without umlauts or accents: a WPA passphrase is printable ASCII.

static constexpr const char* WIFI_EN_LOWER[] = { EN_LOWER_ROWS, ROW4_WIFI };
static constexpr const char* WIFI_EN_UPPER[] = { EN_UPPER_ROWS, ROW4_WIFI };
// German: English with z and y swapped.
static constexpr const char* WIFI_DE_LOWER[] = {
  "1#", "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", KEY_SYM_BACKSPACE, "\n",
  "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", KEY_SYM_NEW_LINE, "\n",
  "_", "-", "y", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
  ROW4_WIFI
};
static constexpr const char* WIFI_DE_UPPER[] = {
  "1#", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", KEY_SYM_BACKSPACE, "\n",
  "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", KEY_SYM_NEW_LINE, "\n",
  "_", "-", "Y", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
  ROW4_WIFI
};
static constexpr uint8_t WIFI_TEXT_W[] = { EN_ROWS_W, ROW4_WIFI_W };
static constexpr uint8_t WIFI_TEXT_F[] = { EN_ROWS_F, ROW4_WIFI_F };
// French: English with a and q, z and w swapped, m at the end of row 2.
static constexpr const char* WIFI_FR_LOWER[] = {
  "1#", "a", "z", "e", "r", "t", "y", "u", "i", "o", "p", KEY_SYM_BACKSPACE, "\n",
  "ABC", "q", "s", "d", "f", "g", "h", "j", "k", "l", "m", KEY_SYM_NEW_LINE, "\n",
  "_", "-", "w", "x", "c", "v", "b", "n", ".", ",", ":", "\n",
  ROW4_WIFI
};
static constexpr const char* WIFI_FR_UPPER[] = {
  "1#", "A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P", KEY_SYM_BACKSPACE, "\n",
  "abc", "Q", "S", "D", "F", "G", "H", "J", "K", "L", "M", KEY_SYM_NEW_LINE, "\n",
  "_", "-", "W", "X", "C", "V", "B", "N", ".", ",", ":", "\n",
  ROW4_WIFI
};
static constexpr uint8_t WIFI_FR_TEXT_W[] = {
  5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7,
  6, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 7,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  ROW4_WIFI_W
};
static constexpr uint8_t WIFI_FR_TEXT_F[] = {
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CHK, CHK, LET, LET, LET, LET, LET, LET, CHK, CHK, CHK,
  ROW4_WIFI_F
};
static constexpr const char* WIFI_SPECIAL[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "^", "~", KEY_SYM_BACKSPACE, "\n",
  "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
  "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "|", "`", "\n",
  ROW4_WIFI
};
static constexpr uint8_t WIFI_SPECIAL_W[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
  2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  ROW4_WIFI_W
};
static constexpr uint8_t WIFI_SPECIAL_F[] = {
  LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, CHK,
  CTL, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET,
  LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET, LET,
  ROW4_WIFI_F
};

// ============================================================
//  THE CHECK OF EVERY PAGE, AT COMPILE TIME
// ============================================================

static constexpr bool isRowBreak(const char* k) {
  return k[0] == '\n' && k[1] == '\0';
}

// Whether a page is whole: the keys end in "", there are as many widths and
// flags as keys and no more than KEY_LAYOUT_MAX_KEYS, no row is longer than
// KEY_ROW_MAX, every flag is one of KeyFlag and every width one LVGL holds.
// LVGL 8.3.11 keeps the width in the low four bits of a key's control word
// (LV_BTNMATRIX_WIDTH_MASK 0x000F, lv_btnmatrix.c line 26): a 0 is drawn as
// 1, and a 16 would set the bit above, LV_BTNMATRIX_CTRL_HIDDEN.
template <size_t NK, size_t NW, size_t NF>
static constexpr bool pageWhole(const char* const (&keys)[NK], const uint8_t (&widths)[NW],
                                const uint8_t (&flags)[NF]) {
  if (NW != NF || NW > KEY_LAYOUT_MAX_KEYS || keys[NK - 1][0] != '\0') return false;
  size_t key = 0;
  int in_row = 0;
  for (size_t i = 0; i + 1 < NK; i++) {
    if (isRowBreak(keys[i])) {
      in_row = 0;
      continue;
    }
    if (!keys[i][0] || ++in_row > KEY_ROW_MAX || key >= NW) return false;
    if (widths[key] < 1 || widths[key] > 15 || flags[key] > KF_PLAIN) return false;
    key++;
  }
  return key == NW;
}

static_assert(pageWhole(EN_LOWER, EN_TEXT_W, EN_TEXT_F), "EN lower is not whole");
static_assert(pageWhole(EN_UPPER, EN_TEXT_W, EN_TEXT_F), "EN upper is not whole");
static_assert(pageWhole(SPECIAL, SPECIAL_W, SPECIAL_F), "special is not whole");
static_assert(pageWhole(DE_LOWER, DE_TEXT_W, DE_TEXT_F), "DE lower is not whole");
static_assert(pageWhole(DE_UPPER, DE_TEXT_W, DE_TEXT_F), "DE upper is not whole");
static_assert(pageWhole(FR_LOWER, FR_TEXT_W, FR_TEXT_F), "FR lower is not whole");
static_assert(pageWhole(FR_UPPER, FR_TEXT_W, FR_TEXT_F), "FR upper is not whole");
static_assert(pageWhole(FR_ACCENTS, FR_ACCENTS_W, FR_ACCENTS_F), "FR accents is not whole");
static_assert(pageWhole(WIFI_EN_LOWER, WIFI_TEXT_W, WIFI_TEXT_F), "WiFi EN lower is not whole");
static_assert(pageWhole(WIFI_EN_UPPER, WIFI_TEXT_W, WIFI_TEXT_F), "WiFi EN upper is not whole");
static_assert(pageWhole(WIFI_DE_LOWER, WIFI_TEXT_W, WIFI_TEXT_F), "WiFi DE lower is not whole");
static_assert(pageWhole(WIFI_DE_UPPER, WIFI_TEXT_W, WIFI_TEXT_F), "WiFi DE upper is not whole");
static_assert(pageWhole(WIFI_FR_LOWER, WIFI_FR_TEXT_W, WIFI_FR_TEXT_F), "WiFi FR lower is not whole");
static_assert(pageWhole(WIFI_FR_UPPER, WIFI_FR_TEXT_W, WIFI_FR_TEXT_F), "WiFi FR upper is not whole");
static_assert(pageWhole(WIFI_SPECIAL, WIFI_SPECIAL_W, WIFI_SPECIAL_F), "WiFi special is not whole");

// ============================================================
//  THE INDEX
// ============================================================

struct KeyPageEntry {
  KeyLang lang;
  KeyPage page;
  bool wifi;
  KeyMap map;
};

// The number of keys of a page, from its widths: the static_asserts above
// hold it to the keys.
template <size_t N>
static constexpr int keyCount(const uint8_t (&)[N]) {
  return (int)N;
}

static const KeyPageEntry PAGES[] = {
  { KL_EN, KP_LOWER,   false, { EN_LOWER,      EN_TEXT_W,      EN_TEXT_F,      keyCount(EN_TEXT_W) } },
  { KL_EN, KP_UPPER,   false, { EN_UPPER,      EN_TEXT_W,      EN_TEXT_F,      keyCount(EN_TEXT_W) } },
  { KL_EN, KP_SPECIAL, false, { SPECIAL,       SPECIAL_W,      SPECIAL_F,      keyCount(SPECIAL_W) } },
  { KL_DE, KP_LOWER,   false, { DE_LOWER,      DE_TEXT_W,      DE_TEXT_F,      keyCount(DE_TEXT_W) } },
  { KL_DE, KP_UPPER,   false, { DE_UPPER,      DE_TEXT_W,      DE_TEXT_F,      keyCount(DE_TEXT_W) } },
  { KL_DE, KP_SPECIAL, false, { SPECIAL,       SPECIAL_W,      SPECIAL_F,      keyCount(SPECIAL_W) } },
  { KL_FR, KP_LOWER,   false, { FR_LOWER,      FR_TEXT_W,      FR_TEXT_F,      keyCount(FR_TEXT_W) } },
  { KL_FR, KP_UPPER,   false, { FR_UPPER,      FR_TEXT_W,      FR_TEXT_F,      keyCount(FR_TEXT_W) } },
  { KL_FR, KP_SPECIAL, false, { SPECIAL,       SPECIAL_W,      SPECIAL_F,      keyCount(SPECIAL_W) } },
  { KL_FR, KP_ACCENTS, false, { FR_ACCENTS,    FR_ACCENTS_W,   FR_ACCENTS_F,   keyCount(FR_ACCENTS_W) } },
  { KL_EN, KP_LOWER,   true,  { WIFI_EN_LOWER, WIFI_TEXT_W,    WIFI_TEXT_F,    keyCount(WIFI_TEXT_W) } },
  { KL_EN, KP_UPPER,   true,  { WIFI_EN_UPPER, WIFI_TEXT_W,    WIFI_TEXT_F,    keyCount(WIFI_TEXT_W) } },
  { KL_EN, KP_SPECIAL, true,  { WIFI_SPECIAL,  WIFI_SPECIAL_W, WIFI_SPECIAL_F, keyCount(WIFI_SPECIAL_W) } },
  { KL_DE, KP_LOWER,   true,  { WIFI_DE_LOWER, WIFI_TEXT_W,    WIFI_TEXT_F,    keyCount(WIFI_TEXT_W) } },
  { KL_DE, KP_UPPER,   true,  { WIFI_DE_UPPER, WIFI_TEXT_W,    WIFI_TEXT_F,    keyCount(WIFI_TEXT_W) } },
  { KL_DE, KP_SPECIAL, true,  { WIFI_SPECIAL,  WIFI_SPECIAL_W, WIFI_SPECIAL_F, keyCount(WIFI_SPECIAL_W) } },
  { KL_FR, KP_LOWER,   true,  { WIFI_FR_LOWER, WIFI_FR_TEXT_W, WIFI_FR_TEXT_F, keyCount(WIFI_FR_TEXT_W) } },
  { KL_FR, KP_UPPER,   true,  { WIFI_FR_UPPER, WIFI_FR_TEXT_W, WIFI_FR_TEXT_F, keyCount(WIFI_FR_TEXT_W) } },
  { KL_FR, KP_SPECIAL, true,  { WIFI_SPECIAL,  WIFI_SPECIAL_W, WIFI_SPECIAL_F, keyCount(WIFI_SPECIAL_W) } },
};

bool keyLayoutGet(KeyLang lang, KeyPage page, bool wifi, KeyMap* out) {
  for (const KeyPageEntry& p : PAGES) {
    if (p.lang != lang || p.page != page || p.wifi != wifi) continue;
    if (out) *out = p.map;
    return true;
  }
  return false;
}
