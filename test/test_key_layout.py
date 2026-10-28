"""Compile the production keyboard layouts (w02, V43) and check them key by key: English is LVGL's own keyboard, German QWERTZ with the umlauts and the sharp s, French AZERTY with a page of accents; the WiFi password gets the letters of the language without umlauts or accents, the signs it always had and no cancel key. Every page: the keys in the format of lv_btnmatrix_set_map(), a width and a flag for each key, at most 14 keys to a row, and no character the fonts cannot draw (services/key_layout.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# ---------------------------------------------------------------------------
#  What the pages hold, spelled out here on purpose: the production tables
#  must agree with this text, not with themselves.
# ---------------------------------------------------------------------------

# The control keys, as LVGL 8.3.11 spells them (src/font/lv_symbol_def.h:
# LV_SYMBOL_BACKSPACE line 264, NEW_LINE 272, KEYBOARD 212, LEFT 124, RIGHT
# 128, OK 48), and the key of the accent page.
BS = b"\xEF\x95\x9A".decode()        # LV_SYMBOL_BACKSPACE, U+F55A
NL = b"\xEF\xA2\xA2".decode()        # LV_SYMBOL_NEW_LINE, U+F8A2
KB = b"\xEF\x84\x9C".decode()        # LV_SYMBOL_KEYBOARD, U+F11C
LT = b"\xEF\x81\x93".decode()        # LV_SYMBOL_LEFT, U+F053
RT = b"\xEF\x81\x94".decode()        # LV_SYMBOL_RIGHT, U+F054
OK = b"\xEF\x80\x8C".decode()        # LV_SYMBOL_OK, U+F00C
ACC = "àé"                 # KEY_ACCENTS, the two letters a-grave e-acute
SYMBOLS = (("KEY_SYM_BACKSPACE", BS), ("KEY_SYM_NEW_LINE", NL), ("KEY_SYM_KEYBOARD", KB),
           ("KEY_SYM_LEFT", LT), ("KEY_SYM_RIGHT", RT), ("KEY_SYM_OK", OK), ("KEY_ACCENTS", ACC))

LETTER, CHECKED, CTRL, PLAIN = "KF_LETTER", "KF_CHECKED", "KF_CTRL", "KF_PLAIN"


def keys(text, width, flag):
    """Keys of one width and flag, written with a blank between them."""
    return [(k, width, flag) for k in text.split(" ")]


# English: LVGL's default_kb_map_lc/uc/spec and their control maps
# (.pio/libdeps/<board>/lvgl/src/extra/widgets/keyboard/lv_keyboard.c, lines
# 49 to 86 in 8.3.11). Row 1: the page key 5, the letters 4, delete 7;
# row 2: the page key 6, the letters 3, new line 7; row 3: all 1, the signs
# checked; row 4: keyboard 2, left 2, space 6, right 2, OK 2.
ROW4 = [(KB, 2, CTRL), (LT, 2, CHECKED), (" ", 6, PLAIN), (RT, 2, CHECKED), (OK, 2, CTRL)]


def text_page(page_key, row1, back_key, row2, row3_letters, row4=ROW4, row3_signs=". , :"):
    """A letter page: the page key, ten or eleven letters and delete; the
    other page key, the second row and new line; signs and letters; the
    bottom row."""
    return [[(page_key, 5, CTRL)] + keys(row1, 4, LETTER) + [(BS, 7, CHECKED)],
            [(back_key, 6, CTRL)] + keys(row2, 3, LETTER) + [(NL, 7, CHECKED)],
            keys("_ -", 1, CHECKED) + keys(row3_letters, 1, LETTER) + keys(row3_signs, 1, CHECKED),
            row4]


EN_LOWER = text_page("1#", "q w e r t y u i o p", "ABC", "a s d f g h j k l", "z x c v b n m")
EN_UPPER = text_page("1#", "Q W E R T Y U I O P", "abc", "A S D F G H J K L", "Z X C V B N M")
EN_SPECIAL = [keys("1 2 3 4 5 6 7 8 9 0", 1, LETTER) + [(BS, 2, CHECKED)],
              [("abc", 2, CTRL)] + keys("+ & / * = % ! ? # < >", 1, LETTER),
              keys('\\ @ $ ( ) { } [ ] ; " \'', 1, LETTER),
              ROW4]

# German, QWERTZ (draft 2): u-umlaut after p, o- and a-umlaut after l, the
# sharp s after m, on both letter pages (there is no capital sharp s).
DE_LOWER = text_page("1#", "q w e r t z u i o p ü", "ABC", "a s d f g h j k l ö ä",
                     "y x c v b n m ß")
DE_UPPER = text_page("1#", "Q W E R T Z U I O P Ü", "abc", "A S D F G H J K L Ö Ä",
                     "Y X C V B N M ß")

# French, AZERTY (draft 2): m at the end of row 2, the four accents of every
# day in row 3, and in row 4 the key to the page of the others (2, 2 CTRL, 2,
# 4, 2, 2).
FR_ROW4 = [(KB, 2, CTRL), (ACC, 2, CTRL), (LT, 2, CHECKED), (" ", 4, PLAIN), (RT, 2, CHECKED), (OK, 2, CTRL)]


def fr_page(page_key, row1, back_key, row2, row3_letters):
    return [[(page_key, 5, CTRL)] + keys(row1, 4, LETTER) + [(BS, 7, CHECKED)],
            [(back_key, 6, CTRL)] + keys(row2, 3, LETTER) + [(NL, 7, CHECKED)],
            [("-", 1, CHECKED)] + keys(row3_letters, 1, LETTER) + keys(", .", 1, CHECKED),
            FR_ROW4]


FR_LOWER = fr_page("1#", "a z e r t y u i o p", "ABC", "q s d f g h j k l m",
                   "w x c v b n é è à ç")
FR_UPPER = fr_page("1#", "A Z E R T Y U I O P", "abc", "Q S D F G H J K L M",
                   "W X C V B N É È À Ç")
FR_ACCENTS = [keys("â ê î ô û ë ï ü ù œ", 4, LETTER) + [(BS, 7, CHECKED)],
              [("abc", 6, CTRL)] + keys("Â Ê Î Ô Û Ë Ï Ü Ù Œ", 3, LETTER)
              + [(NL, 7, CHECKED)],
              keys("_ : ' « »", 1, CHECKED),
              FR_ROW4]

# The WiFi password (wifi_pass_screen.cpp before V43): LVGL's letters, a
# special page with ^ ~ | and `, and no keyboard key, which only sends a
# CANCEL nothing on that screen answers; its bottom row is left 2, space 6,
# right 2, OK 4. The letters follow the language, without umlauts or accents.
WIFI_ROW4 = [(LT, 2, CHECKED), (" ", 6, PLAIN), (RT, 2, CHECKED), (OK, 4, CTRL)]
WIFI_SPECIAL = [keys("1 2 3 4 5 6 7 8 9 0 ^ ~", 1, LETTER) + [(BS, 2, CHECKED)],
                [("abc", 2, CTRL)] + keys("+ & / * = % ! ? # < >", 1, LETTER),
                keys('\\ @ $ ( ) { } [ ] ; " \' | `', 1, LETTER),
                WIFI_ROW4]


def wifi(page):
    """The page with the bottom row of the WiFi keyboard."""
    return page[:3] + [WIFI_ROW4]


WIFI_EN_LOWER = wifi(EN_LOWER)
WIFI_EN_UPPER = wifi(EN_UPPER)
# German: English with z and y swapped.
WIFI_DE_LOWER = wifi(text_page("1#", "q w e r t z u i o p", "ABC", "a s d f g h j k l", "y x c v b n m"))
WIFI_DE_UPPER = wifi(text_page("1#", "Q W E R T Z U I O P", "abc", "A S D F G H J K L", "Y X C V B N M"))
# French: English with a and q, z and w swapped, m at the end of row 2.
WIFI_FR_LOWER = wifi(text_page("1#", "a z e r t y u i o p", "ABC", "q s d f g h j k l m", "w x c v b n"))
WIFI_FR_UPPER = wifi(text_page("1#", "A Z E R T Y U I O P", "abc", "Q S D F G H J K L M", "W X C V B N"))

MAPS = [
    ("KL_EN", "KP_LOWER", False, EN_LOWER), ("KL_EN", "KP_UPPER", False, EN_UPPER),
    ("KL_EN", "KP_SPECIAL", False, EN_SPECIAL),
    ("KL_DE", "KP_LOWER", False, DE_LOWER), ("KL_DE", "KP_UPPER", False, DE_UPPER),
    ("KL_DE", "KP_SPECIAL", False, EN_SPECIAL),
    ("KL_FR", "KP_LOWER", False, FR_LOWER), ("KL_FR", "KP_UPPER", False, FR_UPPER),
    ("KL_FR", "KP_SPECIAL", False, EN_SPECIAL), ("KL_FR", "KP_ACCENTS", False, FR_ACCENTS),
    ("KL_EN", "KP_LOWER", True, WIFI_EN_LOWER), ("KL_EN", "KP_UPPER", True, WIFI_EN_UPPER),
    ("KL_EN", "KP_SPECIAL", True, WIFI_SPECIAL),
    ("KL_DE", "KP_LOWER", True, WIFI_DE_LOWER), ("KL_DE", "KP_UPPER", True, WIFI_DE_UPPER),
    ("KL_DE", "KP_SPECIAL", True, WIFI_SPECIAL),
    ("KL_FR", "KP_LOWER", True, WIFI_FR_LOWER), ("KL_FR", "KP_UPPER", True, WIFI_FR_UPPER),
    ("KL_FR", "KP_SPECIAL", True, WIFI_SPECIAL),
]
# Pages that are not there: the accents are French, and not for the WiFi.
MISSING = [("KL_EN", "KP_ACCENTS", False), ("KL_EN", "KP_ACCENTS", True), ("KL_DE", "KP_ACCENTS", False),
           ("KL_DE", "KP_ACCENTS", True), ("KL_FR", "KP_ACCENTS", True)]

# What the fonts draw beyond ASCII. The umlauts and the sharp s are in
# montserrat_ext_* (its "-r" list: 0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC); the
# French letters in fr_supp_*, the fallback of montserrat_ext_* (in
# src/fonts/lv_font_fr_supp_14.c the glyph comments "/* U+00C0" line 46,
# U+00C2 72, U+00C7 109, U+00C8 120, U+00C9 129, U+00CA 138, U+00CB 147,
# U+00CE 169, U+00CF 177, U+00D4 229, U+00D9 271, U+00DB 291, U+00E0 321,
# U+00E2 337, U+00E7 370, U+00E8 378, U+00E9 386, U+00EA 394, U+00EB 402,
# U+00EE 421, U+00EF 428, U+00F4 468, U+00F9 500, U+00FB 516, U+0152 553,
# U+0153 565, U+00AB 36, U+00BB 41). Both files are read below, so that the
# list cannot claim a glyph that is not there.
GERMAN = [0xC4, 0xD6, 0xDC, 0xDF, 0xE4, 0xF6, 0xFC]
FRENCH = [0xC0, 0xC2, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCE, 0xCF, 0xD4, 0xD9, 0xDB,
          0xE0, 0xE2, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEE, 0xEF, 0xF4, 0xF9, 0xFB,
          0x152, 0x153, 0xAB, 0xBB]
host = (src / "fonts" / "lv_font_montserrat_ext_14.c").read_text(encoding="utf-8")
opts = [line for line in host.splitlines() if "Opts:" in line]
assert opts and all(f"0x{cp:X}" in opts[0].split("-r ")[1].split(" ")[0].split(",") for cp in GERMAN), \
    "src/fonts/lv_font_montserrat_ext_14.c: a German letter of the keyboards is not in its range"
supp = (src / "fonts" / "lv_font_fr_supp_14.c").read_text(encoding="utf-8")
for cp in FRENCH:
    assert f"/* U+{cp:04X} " in supp, f"src/fonts/lv_font_fr_supp_14.c: no glyph U+{cp:04X}"
# Every character the tables above use is one of them (or ASCII, or a symbol).
for _, _, _, rows in MAPS:
    for row in rows:
        for k, _, _ in row:
            if k in (BS, NL, KB, LT, RT, OK):
                continue
            for ch in k:
                assert ord(ch) < 0x80 or ord(ch) in GERMAN + FRENCH, f"the test's own table: {k!r}"


def c_string(text):
    """text as a C++ string literal, every byte beyond ASCII as a hex escape."""
    out, hexed = [], False
    for byte in text.encode("utf-8"):
        ch = chr(byte)
        if byte >= 0x80:
            out.append("\\x%02X" % byte)
            hexed = True
            continue
        if hexed and ch in "0123456789abcdefABCDEF":
            out.append('" "')          # a hex escape would swallow the digit
        hexed = False
        out.append({"\\": "\\\\", '"': '\\"', "\n": "\\n"}.get(ch, ch))
    return '"' + "".join(out) + '"'


def c_map(n, lang, page, is_wifi, rows):
    """The arrays of one page and the line that checks it."""
    flat = [k for row in rows for k in row]
    texts = []
    for i, row in enumerate(rows):
        texts += [c_string(k) for k, _, _ in row]
        texts.append('"\\n"' if i < len(rows) - 1 else '""')
    name = f"{lang}/{page}{' wifi' if is_wifi else ''}"
    return (f"static const char* const k{n}[] = {{ {', '.join(texts)} }};\n"
            f"static const uint8_t w{n}[] = {{ {', '.join(str(w) for _, w, _ in flat)} }};\n"
            f"static const uint8_t f{n}[] = {{ {', '.join(f for _, _, f in flat)} }};\n",
            f"  check({{ {lang}, {page}, {'true' if is_wifi else 'false'}, k{n}, w{n}, f{n}, "
            f"{len(flat)}, \"{name}\" }});\n")


arrays, checks = [], []
for n, (lang, page, is_wifi, rows) in enumerate(MAPS):
    a, c = c_map(n, lang, page, is_wifi, rows)
    arrays.append(a)
    checks.append(c)
missing = "".join(f"  assert(!keyLayoutGet({lang}, {page}, {'true' if w else 'false'}, &none));\n"
                  for lang, page, w in MISSING)
symbols = "".join(f"  assert(strcmp({name}, {c_string(text)}) == 0);\n" for name, text in SYMBOLS)
allowed = ", ".join("0x%X" % cp for cp in GERMAN + FRENCH)
control = ", ".join(c_string(t) for t in (BS, NL, KB, LT, RT, OK))

source = r'''
#include "services/key_layout.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// The values the interface names: the order of the languages, the pages and
// the flags is what the panel and the tables share.
static_assert(KL_EN == 0 && KL_DE == 1 && KL_FR == 2, "the languages");
static_assert(KP_LOWER == 0 && KP_UPPER == 1 && KP_SPECIAL == 2 && KP_ACCENTS == 3, "the pages");
static_assert(KF_LETTER == 0 && KF_CHECKED == 1 && KF_CTRL == 2 && KF_PLAIN == 3, "the flags");

struct Want {
  KeyLang lang;
  KeyPage page;
  bool wifi;
  const char* const* keys;     // with "\n" between the rows and "" at the end
  const uint8_t* widths;       // one per key
  const uint8_t* flags;
  int count;                   // the keys, without the row breaks
  const char* name;
};

static const char* const CONTROL[] = { @CONTROL@ };
static const uint32_t ALLOWED[] = { @ALLOWED@ };

static int g_bad = 0;

static void expect(bool ok, const char* map, int key, const char* what) {
  if (ok) return;
  fprintf(stderr, "%s, key %d: %s\n", map, key, what);
  g_bad++;
}

static bool isControl(const char* k) {
  for (const char* c : CONTROL) {
    if (strcmp(k, c) == 0) return true;
  }
  return false;
}

// The code points of k, read as UTF-8; false for a sequence that is broken
// or cut, or a code point not in ALLOWED.
static bool drawable(const char* k) {
  const unsigned char* p = (const unsigned char*)k;
  while (*p) {
    if (*p < 0x80) { p++; continue; }
    int extra = (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0) return false;
    uint32_t cp = *p & (extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07);
    p++;
    for (int i = 0; i < extra; i++, p++) {
      if ((*p & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (*p & 0x3F);
    }
    bool found = false;
    for (uint32_t a : ALLOWED) found = found || a == cp;
    if (!found) return false;
  }
  return true;
}

static bool ascii(const char* k) {
  for (const unsigned char* p = (const unsigned char*)k; *p; p++) {
    if (*p >= 0x80) return false;
  }
  return true;
}

static void check(const Want& w) {
  KeyMap m = {};
  if (!keyLayoutGet(w.lang, w.page, w.wifi, &m)) {
    expect(false, w.name, -1, "keyLayoutGet() has no such page");
    return;
  }
  if (!m.keys || !m.widths || !m.flags) {
    expect(false, w.name, -1, "a table is missing");
    return;
  }
  expect(m.count == w.count, w.name, -1, "count is not the number of keys");
  // The entries as lv_btnmatrix_set_map() reads them, row breaks and the
  // terminator included; a page without its "" would run on.
  int key = 0, row_keys = 0, i = 0;
  for (;; i++) {
    if (i > 80) { expect(false, w.name, key, "no \"\" at the end"); return; }
    const char* got = m.keys[i];
    const char* want = w.keys[i];
    if (!got) { expect(false, w.name, key, "a null entry"); return; }
    expect(strcmp(got, want) == 0, w.name, key, "another key, row break or end");
    if (strcmp(got, want) != 0) return;
    if (!got[0]) break;
    if (strcmp(got, "\n") == 0) { row_keys = 0; continue; }
    row_keys++;
    expect(row_keys <= 14, w.name, key, "more than 14 keys in a row");
    expect(key < m.count, w.name, key, "more keys than count");
    if (key < w.count && key < m.count) {
      expect(m.widths[key] == w.widths[key], w.name, key, "another width");
      expect(m.flags[key] == w.flags[key], w.name, key, "another flag");
    }
    // The WiFi password is typed in ASCII: nothing beyond it but the symbols.
    if (w.wifi) expect(isControl(got) || ascii(got), w.name, key, "a byte of 0x80 or more on the WiFi keys");
    expect(isControl(got) || drawable(got), w.name, key, "a character the fonts do not draw");
    key++;
  }
  expect(key == m.count, w.name, key, "count is not the number of keys in the table");
}

@ARRAYS@
int main() {
  // The symbols the tables use are LVGL's (key_maps.cpp holds them to the
  // LV_SYMBOL_* of the build with a static_assert), and the accent key.
@SYMBOLS@
@CHECKS@
  KeyMap none = {};
@MISSING@
  assert(g_bad == 0);
  return 0;
}
'''
source = (source.replace("@CONTROL@", control).replace("@ALLOWED@", allowed)
          .replace("@ARRAYS@", "".join(arrays)).replace("@SYMBOLS@", symbols)
          .replace("@CHECKS@", "".join(checks)).replace("@MISSING@", missing))

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(src),
                    str(cpp), str(src / "services" / "key_layout.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("key layout: ok")
