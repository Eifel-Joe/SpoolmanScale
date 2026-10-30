"""Compile the production rules for the old location of a spool that goes into a Klipper printer (services/klipper_location_rules.h, V50) and check them: what is remembered in Spoolman's extra field when the spool's location becomes the printer's name (its current location, or the one already remembered when the current one is a printer's name), what the spool that is displaced from its field gets (its remembered location back, an empty one, or nothing when its location is no longer exactly the printer's name), and a text value as Spoolman's extra field holds it (JSON text) and back."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/klipper_location_rules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* g_what = "";
#define WHAT(s) (g_what = (s))
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s] line %d: %s\n", g_what, __LINE__, #c); abort(); } } while (0)
#define CHECK_INT(a, b) do { const long long x_ = (long long)(a); const long long y_ = (long long)(b); \
  if (x_ != y_) { fprintf(stderr, "FAIL [%s] line %d: %s is %lld, expected %lld\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)
#define CHECK_STR(a, b) do { const char* x_ = (a); const char* y_ = (b); \
  if (strcmp(x_, y_) != 0) { fprintf(stderr, "FAIL [%s] line %d: %s is \"%s\", expected \"%s\"\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)

// The printers the scale knows, as klipperStoreAt() would list their names.
static const char* const PRINTERS[] = {"Voron 2.4", "Hulk"};
static const KlipperNames KNOWN = {PRINTERS, 2};

// What klipperPrevToRemember() gives, in a buffer that starts as garbage: a
// rule that leaves it unterminated or unwritten shows.
static const char* remember(const char* current, const char* stored) {
  static char out[KL_LOC_MAX];
  memset(out, 0xA5, sizeof(out));
  CHECK(klipperPrevToRemember(current, stored, KNOWN, out, sizeof(out)));
  CHECK(memchr(out, 0, sizeof(out)) != nullptr);
  return out;
}

// klipperExtraText() and klipperExtraValue() with exactly n bytes of room and
// a fence of known bytes behind them that must still be there afterwards.
static bool textIn(const char* value, size_t n, char* result) {
  char mem[KL_LOC_TEXT_MAX + 16];
  memset(mem, 0x7E, sizeof(mem));
  const bool ok = klipperExtraText(value, mem, n);
  for (size_t k = n; k < sizeof(mem); k++) CHECK(mem[k] == 0x7E);
  CHECK(memchr(mem, 0, n) != nullptr);
  if (!ok) CHECK(mem[0] == '\0');
  memcpy(result, mem, n);
  return ok;
}

static bool valueIn(const char* json_text, size_t n, char* result) {
  char mem[KL_LOC_MAX + 16];
  memset(mem, 0x7E, sizeof(mem));
  const bool ok = klipperExtraValue(json_text, mem, n);
  for (size_t k = n; k < sizeof(mem); k++) CHECK(mem[k] == 0x7E);
  CHECK(memchr(mem, 0, n) != nullptr);
  if (!ok) CHECK(mem[0] == '\0');
  memcpy(result, mem, n);
  return ok;
}

static void roundTrip(const char* value) {
  char text[KL_LOC_TEXT_MAX];
  char back[KL_LOC_MAX];
  CHECK(textIn(value, sizeof(text), text));
  CHECK(valueIn(text, sizeof(back), back));
  CHECK_STR(back, value);
}

int main() {
  WHAT("fixed names and sizes");
  CHECK_STR(KL_PREV_LOC_KEY, "scale_prev_location");
  CHECK(KL_LOC_MAX >= 130);                       // LOCATION_NAME_MAX of the scale
  CHECK(KL_LOC_TEXT_MAX >= 2 * KL_LOC_MAX + 2);    // every quote and backslash doubled, and the quotes
  CHECK_INT(KR_NONE, 0);
  CHECK_INT(KR_RESTORE, 1);
  CHECK_INT(KR_CLEAR, 2);
  CHECK_INT(sizeof(KlipperRestoreAct), 1);

  WHAT("remember: the current location");
  CHECK_STR(remember("Regal", nullptr), "Regal");
  CHECK_STR(remember("Regal", "Keller"), "Regal");       // what was remembered before is replaced
  CHECK_STR(remember("Regal 3, oben", ""), "Regal 3, oben");

  WHAT("remember: the current location is a printer, the remembered one stays");
  CHECK_STR(remember("Hulk", "Regal"), "Regal");
  CHECK_STR(remember("Voron 2.4", "Regal"), "Regal");     // from one printer to the other
  CHECK_STR(remember("Hulk", nullptr), "");
  CHECK_STR(remember("Hulk", ""), "");

  WHAT("remember: an empty location is remembered as empty");
  CHECK_STR(remember("", nullptr), "");
  CHECK_STR(remember("", "Regal"), "");
  CHECK_STR(remember(nullptr, "Regal"), "");

  WHAT("remember: the printer's name is compared byte for byte");
  CHECK_STR(remember("hulk", "Regal"), "hulk");
  CHECK_STR(remember("Hulk ", "Regal"), "Hulk ");
  CHECK_STR(remember("Voron", "Regal"), "Voron");

  WHAT("remember: no printers, a null among them");
  {
    char out[KL_LOC_MAX];
    // Built ahead: a comma inside braces would split the CHECK() macro's argument.
    const KlipperNames empty = {PRINTERS, 0};
    const KlipperNames no_list = {nullptr, 2};
    const char* const gaps[] = {nullptr, "Hulk"};
    const KlipperNames with_gap = {gaps, 2};
    CHECK(klipperPrevToRemember("Hulk", "Regal", empty, out, sizeof(out)));
    CHECK_STR(out, "Hulk");
    CHECK(klipperPrevToRemember("Hulk", "Regal", no_list, out, sizeof(out)));
    CHECK_STR(out, "Hulk");
    CHECK(klipperPrevToRemember("Hulk", "Regal", with_gap, out, sizeof(out)));
    CHECK_STR(out, "Regal");
    CHECK(klipperPrevToRemember("Regal", nullptr, with_gap, out, sizeof(out)));
    CHECK_STR(out, "Regal");
  }

  WHAT("remember: no room");
  {
    char out[8];
    memset(out, 0xA5, sizeof(out));
    CHECK(!klipperPrevToRemember("Regal", nullptr, KNOWN, out, 5));
    CHECK_STR(out, "");
    CHECK(klipperPrevToRemember("Regal", nullptr, KNOWN, out, 6));
    CHECK_STR(out, "Regal");
    memset(out, 0xA5, sizeof(out));
    CHECK(!klipperPrevToRemember("Hulk", "Regal", KNOWN, out, 5));
    CHECK_STR(out, "");
    CHECK(!klipperPrevToRemember("Regal", nullptr, KNOWN, nullptr, 8));
    CHECK(!klipperPrevToRemember("Regal", nullptr, KNOWN, out, 0));
  }

  WHAT("restore: the displaced spool still has the printer's name");
  CHECK_INT(klipperRestoreFor("Hulk", "Hulk", "Regal"), KR_RESTORE);
  CHECK_INT(klipperRestoreFor("Voron 2.4", "Voron 2.4", "Regal 3, oben"), KR_RESTORE);
  CHECK_INT(klipperRestoreFor("Hulk", "Hulk", nullptr), KR_CLEAR);
  CHECK_INT(klipperRestoreFor("Hulk", "Hulk", ""), KR_CLEAR);

  WHAT("restore: any other location is left alone");
  CHECK_INT(klipperRestoreFor("Regal", "Hulk", "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor("hulk", "Hulk", "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor("Hulk ", "Hulk", "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor("Voron 2.4", "Hulk", "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor("", "Hulk", "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor(nullptr, "Hulk", "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor("", "", "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor("Hulk", nullptr, "Regal"), KR_NONE);
  CHECK_INT(klipperRestoreFor("Hulk", nullptr, nullptr), KR_NONE);

  WHAT("extra text: a JSON string literal");
  {
    char text[KL_LOC_TEXT_MAX];
    CHECK(textIn("Regal", sizeof(text), text));
    CHECK_STR(text, "\"Regal\"");
    CHECK(textIn("", sizeof(text), text));
    CHECK_STR(text, "\"\"");
    CHECK(textIn(nullptr, sizeof(text), text));
    CHECK_STR(text, "\"\"");
    CHECK(textIn("Fach \"A\"", sizeof(text), text));
    CHECK_STR(text, "\"Fach \\\"A\\\"\"");
    CHECK(textIn("C:\\Regal", sizeof(text), text));
    CHECK_STR(text, "\"C:\\\\Regal\"");
    CHECK(textIn("a\nb\tc", sizeof(text), text));
    CHECK_STR(text, "\"a\\nb\\tc\"");
    CHECK(textIn("x\x01y", sizeof(text), text));
    CHECK_STR(text, "\"x\\u0001y\"");
    CHECK(textIn("K\xC3\xBChlschrank", sizeof(text), text));    // UTF-8 passes as it is
    CHECK_STR(text, "\"K\xC3\xBChlschrank\"");
  }

  WHAT("extra text: no room");
  {
    char text[KL_LOC_TEXT_MAX];
    CHECK(!textIn("Regal", 7, text));    // "Regal" with its quotes is 7 bytes, the end needs one more
    CHECK(textIn("Regal", 8, text));
    CHECK_STR(text, "\"Regal\"");
    CHECK(!textIn("a\"b", 6, text));     // the escaped quote needs two bytes
    CHECK(textIn("a\"b", 7, text));
    CHECK_STR(text, "\"a\\\"b\"");
    CHECK(!textIn("", 2, text));
    CHECK(!klipperExtraText("Regal", nullptr, 8));
    CHECK(!klipperExtraText("Regal", text, 0));
  }

  WHAT("extra value: the JSON text read back");
  {
    char back[KL_LOC_MAX];
    CHECK(valueIn("\"Regal\"", sizeof(back), back));
    CHECK_STR(back, "Regal");
    CHECK(valueIn("\"\"", sizeof(back), back));
    CHECK_STR(back, "");
    CHECK(valueIn(" \"Regal\" ", sizeof(back), back));          // blanks around the JSON text
    CHECK_STR(back, "Regal");
    CHECK(valueIn("\"Fach \\\"A\\\"\"", sizeof(back), back));
    CHECK_STR(back, "Fach \"A\"");
    CHECK(valueIn("\"a\\/b\\\\c\"", sizeof(back), back));
    CHECK_STR(back, "a/b\\c");
    CHECK(valueIn("\"K\\u00fchl\"", sizeof(back), back));      // a writer that escapes what is not ASCII
    CHECK_STR(back, "K\xC3\xBChl");
    CHECK(valueIn("\"\\u20ac\"", sizeof(back), back));
    CHECK_STR(back, "\xE2\x82\xAC");
    CHECK(valueIn("\"\\ud83d\\ude00\"", sizeof(back), back));   // a pair of surrogates is one character
    CHECK_STR(back, "\xF0\x9F\x98\x80");
  }

  WHAT("extra value: what is no JSON string");
  {
    char back[KL_LOC_MAX];
    CHECK(!valueIn(nullptr, sizeof(back), back));
    CHECK(!valueIn("", sizeof(back), back));
    CHECK(!valueIn("Regal", sizeof(back), back));
    CHECK(!valueIn("\"Regal", sizeof(back), back));
    CHECK(!valueIn("\"Regal\" x", sizeof(back), back));
    CHECK(!valueIn("\"Re\\qgal\"", sizeof(back), back));
    CHECK(!valueIn("\"Re\x01gal\"", sizeof(back), back));       // a raw control character
    CHECK(!valueIn("\"\\u00\"", sizeof(back), back));
    CHECK(!valueIn("\"\\u00zz\"", sizeof(back), back));
    CHECK(!valueIn("\"\\u0000\"", sizeof(back), back));         // would end the text early
    CHECK(!valueIn("\"\\ud83d\"", sizeof(back), back));         // half a pair
    CHECK(!valueIn("\"\\ude00\"", sizeof(back), back));
    CHECK(!valueIn("\"\\ud83d\\u0041\"", sizeof(back), back));
    CHECK(!valueIn("12", sizeof(back), back));
    CHECK(!valueIn("null", sizeof(back), back));
  }

  WHAT("extra value: no room");
  {
    char back[KL_LOC_MAX];
    CHECK(!valueIn("\"Regal\"", 5, back));
    CHECK(valueIn("\"Regal\"", 6, back));
    CHECK_STR(back, "Regal");
    CHECK(!valueIn("\"K\\u00fchl\"", 5, back));    // the escape is two bytes of UTF-8
    CHECK(valueIn("\"K\\u00fchl\"", 6, back));
    CHECK(!klipperExtraValue("\"Regal\"", nullptr, 8));
    CHECK(!klipperExtraValue("\"Regal\"", back, 0));
  }

  WHAT("extra text and value: there and back");
  roundTrip("Regal");
  roundTrip("");
  roundTrip("Fach \"A\"");
  roundTrip("C:\\Regal\\");
  roundTrip("\"\\\"");
  roundTrip("K\xC3\xBChlschrank \xE2\x82\xAC");
  roundTrip("a\nb\r\tc\x1f");
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp),
                    str(root / "src" / "services" / "klipper_location_rules.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("test_klipper_location_rules: ok")
