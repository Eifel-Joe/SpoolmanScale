"""Compile the production rule for which storage locations the scale's location picker leaves out (services/klipper_hide_rules.h, V79) and check it: the name of a Klipper printer from the scale's Klipper page, compared the way that page compares two printer names (case and accents do not count, nor blanks at the ends), and with a Happy Hare printer stored also Happy Hare's own locations of the form "<host> @ MMU Gate:<n>"; a location left out does not count against the limit of the list. Then check that the picker on the device asks this rule before the limit, that it is the only place that fetches the list of locations, and that the list of printers is empty where the scale has no Klipper printers."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/klipper_hide_rules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* g_what = "";
#define WHAT(s) (g_what = (s))
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s] line %d: %s\n", g_what, __LINE__, #c); abort(); } } while (0)
#define CHECK_INT(a, b) do { const long long x_ = (long long)(a); const long long y_ = (long long)(b); \
  if (x_ != y_) { fprintf(stderr, "FAIL [%s] line %d: %s is %lld, expected %lld\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)

// The printers of the Klipper page, as klipperStoreAt() lists their names.
static const char* const PRINTERS[] = {"Hulk", "bÜro", nullptr, "Voron 2.4", "Atelier Café"};
static const KlipperHide PAGE    = {{PRINTERS, 5}, false};
static const KlipperHide PAGE_HH = {{PRINTERS, 5}, true};
static const KlipperHide NONE    = {{nullptr, 0}, false};
static const KlipperHide HH_ONLY = {{nullptr, 0}, true};

// One pass of the picker over locs[0..n): how many rows it shows and whether
// the limit cut one, the way fetchAndFillLocationList() runs it.
static int pass(const char* const* locs, int n, const KlipperHide& hide, int limit, bool* cut,
                int* hidden) {
  int shown = 0;
  *cut = false;
  *hidden = 0;
  for (int i = 0; i < n; i++) {
    const KlipperLocRow r = klipperLocationRow(locs[i], hide, shown, limit);
    if (r == KLR_HIDE) { (*hidden)++; continue; }
    if (r == KLR_CUT) { *cut = true; break; }
    shown++;
  }
  return shown;
}

int main() {
  WHAT("the name of a printer, as the page has it");
  CHECK(klipperLocationHidden("Hulk", PAGE));
  CHECK(klipperLocationHidden("Voron 2.4", PAGE));

  WHAT("E1: case, accents and blanks at the ends do not count");
  CHECK(klipperLocationHidden("hulk", PAGE));
  CHECK(klipperLocationHidden("HULK", PAGE));
  CHECK(klipperLocationHidden("voron 2.4", PAGE));
  CHECK(klipperLocationHidden("Büro", PAGE));          // the page has "bÜro"
  CHECK(klipperLocationHidden("BÜRO", PAGE));
  CHECK(klipperLocationHidden("atelier cafe", PAGE));  // the accent folded away
  CHECK(klipperLocationHidden("ATELIER CAFÉ", PAGE));
  CHECK(!klipperLocationHidden("Buro", PAGE));         // an umlaut is a letter of its own, as on the page
  CHECK(klipperLocationHidden(" Hulk", PAGE));
  CHECK(klipperLocationHidden("Hulk ", PAGE));
  CHECK(klipperLocationHidden("\tHulk  ", PAGE));

  WHAT("a shelf stays, also one with a printer's name in it");
  CHECK(!klipperLocationHidden("Regal", PAGE));
  CHECK(!klipperLocationHidden("Hulk Regal", PAGE));
  CHECK(!klipperLocationHidden("Regal Hulk", PAGE));
  CHECK(!klipperLocationHidden("Hulk2", PAGE));
  CHECK(!klipperLocationHidden("Voron", PAGE));
  CHECK(!klipperLocationHidden("Bü ro", PAGE));
  CHECK(!klipperLocationHidden("H ulk", PAGE));

  WHAT("nothing to compare with, or nothing to compare");
  CHECK(!klipperLocationHidden("Hulk", NONE));
  CHECK(!klipperLocationHidden("", PAGE));
  CHECK(!klipperLocationHidden("   ", PAGE));
  CHECK(!klipperLocationHidden(nullptr, PAGE));
  const KlipperHide null_list = {{nullptr, 3}, false};
  CHECK(!klipperLocationHidden("Hulk", null_list));

  WHAT("E3: Happy Hare's own locations, only with a Happy Hare printer");
  CHECK(klipperLocationHidden("voron @ MMU Gate:0", PAGE_HH));
  CHECK(klipperLocationHidden("voron @ MMU Gate:3", HH_ONLY));
  CHECK(klipperLocationHidden("mainsailos.local @ MMU Gate:11", HH_ONLY));
  CHECK(klipperLocationHidden(" voron @ MMU Gate:3 ", HH_ONLY));
  CHECK(!klipperLocationHidden("voron @ MMU Gate:3", PAGE));
  CHECK(!klipperLocationHidden("voron @ MMU Gate:3", NONE));
  CHECK(!klipperLocationHidden(" @ MMU Gate:3", HH_ONLY));      // no host in front
  CHECK(!klipperLocationHidden("@ MMU Gate:3", HH_ONLY));
  CHECK(!klipperLocationHidden("voron @ MMU Gate:", HH_ONLY));  // no number
  CHECK(!klipperLocationHidden("voron @ MMU Gate:3a", HH_ONLY));
  CHECK(!klipperLocationHidden("voron @ MMU Gate:-1", HH_ONLY));
  CHECK(!klipperLocationHidden("voron @ mmu gate:3", HH_ONLY)); // Happy Hare writes it this way only
  CHECK(!klipperLocationHidden("voron @ MMU Gate:3 Regal", HH_ONLY));
  CHECK(!klipperLocationHidden("Regal", HH_ONLY));
  CHECK(klipperLocationHidden("Hulk", PAGE_HH));                 // the names count as well

  WHAT("the row: left out comes before the limit");
  CHECK_INT(klipperLocationRow("Regal", PAGE, 0, 5), KLR_SHOW);
  CHECK_INT(klipperLocationRow("Regal", PAGE, 4, 5), KLR_SHOW);
  CHECK_INT(klipperLocationRow("Regal", PAGE, 5, 5), KLR_CUT);
  CHECK_INT(klipperLocationRow("Hulk", PAGE, 0, 5), KLR_HIDE);
  CHECK_INT(klipperLocationRow("Hulk", PAGE, 5, 5), KLR_HIDE);  // past the limit: no cut for a printer
  CHECK_INT(klipperLocationRow("Hulk", NONE, 5, 5), KLR_CUT);
  CHECK_INT(klipperLocationRow(nullptr, PAGE, 0, 5), KLR_SHOW);   // the picker shows it as "-"

  WHAT("a pass: printers use up no room and never cut");
  {
    bool cut; int hidden;
    const char* const front[] = {"Hulk", "Voron 2.4", "A", "B", "C", "D", "E"};
    CHECK_INT(pass(front, 7, PAGE, 5, &cut, &hidden), 5);
    CHECK(!cut);
    CHECK_INT(hidden, 2);
    const char* const back[] = {"A", "B", "C", "D", "E", "Hulk", "hulk "};
    CHECK_INT(pass(back, 7, PAGE, 5, &cut, &hidden), 5);
    CHECK(!cut);                       // only printers behind the limit: nothing missing
    CHECK_INT(hidden, 2);
    const char* const over[] = {"A", "Hulk", "B", "C", "D", "E", "F"};
    CHECK_INT(pass(over, 7, PAGE, 5, &cut, &hidden), 5);
    CHECK(cut);                        // a shelf is really missing
    CHECK_INT(hidden, 1);
    const char* const only[] = {"Hulk", "Büro"};
    CHECK_INT(pass(only, 2, PAGE, 5, &cut, &hidden), 0);
    CHECK(!cut);
    CHECK_INT(hidden, 2);
    CHECK_INT(pass(only, 2, NONE, 5, &cut, &hidden), 2);   // no Klipper: as before
    CHECK_INT(hidden, 0);
  }
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp),
                    str(root / "src" / "services" / "klipper_hide_rules.cpp"),
                    str(root / "src" / "services" / "klipper_store_rules.cpp"),
                    str(root / "src" / "services" / "text_util.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def body(text, signature):
    """The body of the function that starts with signature, by its braces."""
    start = text.index(signature)
    open_ = text.index("{", start)
    depth = 0
    for i in range(open_, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_:i + 1]
    raise AssertionError(f"no end for {signature}")


def code(text):
    """The text without // comments, so a comment cannot pass a check."""
    return re.sub(r"//[^\n]*", "", text)


screen = code((root / "src" / "ui" / "more_info_screen.cpp").read_text(encoding="utf-8"))
fill = body(screen, "void fetchAndFillLocationList()")

# The picker asks the rule for every location, and the rule decides before the
# limit: the old line that broke off at the limit for any location is gone.
assert "klipperPickerHide(" in fill, "the picker does not fetch the printers to leave out"
assert "klipperLocationRow(" in fill, "the picker does not ask klipperLocationRow()"
assert not re.search(r"if\s*\(\s*loc_shown\s*>=\s*location_list_limit\s*\)", fill), \
    "the old limit check is still there and counts printers"
loop = fill[fill.index("klipperLocationRow("):]
assert loop.index("KLR_HIDE") < loop.index("KLR_CUT"), "a printer is cut before it is left out"
# A list of nothing but printers is an empty list: no "no location" row alone.
pre = fill[:fill.index("lv_btn_create(list)")]
assert "klipperLocationHidden(" in pre, "a list of only printers is not treated as empty"
assert "STR_LOCATION_NO_LOCATIONS" in pre

# Every way to the picker goes through this one list: nothing else asks the
# backend for its locations.
callers = []
for path in (root / "src").rglob("*.cpp"):
    if path.name == "backend_api.cpp":
        continue
    if "backendGetLocationsJson(" in code(path.read_text(encoding="utf-8")):
        callers.append(path.relative_to(root).as_posix())
assert callers == ["src/ui/more_info_screen.cpp"], callers

# Only where the scale has Klipper printers: without klipperAvailable() the
# list of printers is empty and nothing is left out.
loc = code((root / "src" / "services" / "klipper_location.cpp").read_text(encoding="utf-8"))
hide = body(loc, "bool klipperPickerHide(")
assert "klipperAvailable()" in hide, "the printers are left out in every backend"
assert hide.index("klipperAvailable()") < hide.index("klipperStoreAt("), \
    "the store is read before the backend is asked"
assert "KL_LAYOUT_MMU" in hide, "Happy Hare printers are not told"

print("test_klipper_hide_rules: ok")
