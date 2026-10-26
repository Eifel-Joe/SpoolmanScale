"""Compile the production rules of the filament name BamBuddy keeps and check them: the material, then the line when there is one, cut at a UTF-8 boundary, the round trip through the way BamBuddy reads a line out of a name, and the colour that finds such a filament again since the name no longer tells colours apart (services/bambuddy_name.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/bambuddy_name.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// True when the text is well formed UTF-8: every lead byte is followed by
// its continuation bytes and by nothing else. A cut through a sequence fails.
static bool utf8Ok(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    int extra = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0) return false;
    p++;
    for (int i = 0; i < extra; i++, p++) {
      if ((*p & 0xC0) != 0x80) return false;
    }
  }
  return true;
}

// The name for a material and a line, in a buffer of the given size. The
// buffer starts as garbage so that a name the function does not end shows.
static std::string nameIn(const char* material, const char* line, size_t size) {
  char buf[256];
  memset(buf, 0x5A, sizeof(buf));
  assert(size > 0 && size <= sizeof(buf));
  bambuddyFilamentName(material, line, buf, size);
  assert(memchr(buf, '\0', size) != nullptr);   // always ended inside the buffer
  return std::string(buf);
}

static std::string nameOf(const char* material, const char* line) {
  return nameIn(material, line, 64);
}

static std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
  return s.substr(a, b - a);
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }

// How BamBuddy reads a filament: the material in front is taken away (case
// and blanks do not matter) and what is left, without blanks, is the line;
// a name that does not start with the material is the line as a whole.
static std::string lineBamBuddyReads(const std::string& name, const std::string& material) {
  bool starts = name.size() >= material.size();
  for (size_t i = 0; starts && i < material.size(); i++) {
    if (lower(name[i]) != lower(material[i])) starts = false;
  }
  return trim(starts ? name.substr(material.size()) : name);
}

// The round trip: what BamBuddy reads out of the name is the line it was
// given (nothing when there was none), and the name starts with the material.
static void roundTrips(const char* material, const char* line) {
  std::string name = nameOf(material, line);
  std::string mat = trim(material);
  assert(name.size() >= mat.size());
  for (size_t i = 0; i < mat.size(); i++) assert(lower(name[i]) == lower(mat[i]));
  assert(lineBamBuddyReads(name, mat) == trim(line));
}

int main() {
  // The name: the material, then the line when there is one.
  assert(nameOf("ABS", "") == "ABS");
  assert(nameOf("ABS", "Matte") == "ABS Matte");
  assert(nameOf("PLA", "Tough+") == "PLA Tough+");
  assert(nameOf("PETG", "PETG-CF") == "PETG PETG-CF");
  // Only blanks are no line, blanks around the material do not count.
  assert(nameOf("ABS", "  ") == "ABS");
  assert(nameOf(" ABS ", "Matte") == "ABS Matte");

  // Round trip through BamBuddy's reading, for the names above and for a few
  // more: the line comes back as it went in, and an empty one as nothing.
  roundTrips("ABS", "");
  roundTrips("ABS", "Matte");
  roundTrips("PLA", "Tough+");
  roundTrips("PETG", "PETG-CF");
  roundTrips("ABS", "  ");
  roundTrips(" ABS ", "Matte");
  roundTrips("PLA", "Test Mint");
  roundTrips("pla", "Silk");
  roundTrips("PLA+", "Basic");

  // The cut. TagCreateInput::material holds 16 bytes: with a line of thirty
  // capital A umlauts (two bytes each) the name does not fit 64 bytes, and
  // what is left is at most 63 bytes and no half sequence.
  const char* mat16 = "ABCDEFGHIJKLMNOP";
  std::string cut = nameOf(mat16, "\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84"
                                  "\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84"
                                  "\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84");
  assert(cut.size() <= 63);
  assert(utf8Ok(cut.c_str()));
  assert(cut.compare(0, 17, "ABCDEFGHIJKLMNOP ") == 0);
  // With a material of 15 bytes the cut falls inside an umlaut (15 + 1 + 47):
  // the whole sequence goes, not half of it.
  std::string odd = nameOf("ABCDEFGHIJKLMNO", "\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84"
                                              "\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84"
                                              "\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84\xC3\x84");
  assert(odd.size() == 62);
  assert(utf8Ok(odd.c_str()));
  // Whatever the size of the buffer: ended inside it, never through a sequence.
  const char* wide = "\xC3\x84\xE2\x84\xA2\xC3\x96\xF0\x9F\x98\x80";   // A umlaut, trade mark, O umlaut, an emoji
  for (size_t size = 1; size <= 40; size++) {
    std::string n = nameIn("PLA", wide, size);
    assert(n.size() < size);
    assert(utf8Ok(n.c_str()));
    size_t k = n.size() < 3 ? n.size() : 3;
    assert(n.compare(0, k, "PLA", k) == 0);   // the material comes first, as far as it fits
  }
  // No room, no write.
  char sentinel[4] = {'x', 'y', 'z', '\0'};
  bambuddyFilamentName("ABS", "Matte", sentinel, 0);
  assert(strcmp(sentinel, "xyz") == 0);

  // The colour. A single colour wanted: the six digits, with or without the
  // '#', in any case, and an alpha pair behind them does not count.
  assert(bambuddyColourFits("000000", "", false, "000000", ""));
  assert(bambuddyColourFits("000000", nullptr, false, "000000", nullptr));
  assert(bambuddyColourFits("000000FF", "", false, "000000", ""));
  assert(bambuddyColourFits("#90ee90", "", false, "90EE90", ""));
  assert(bambuddyColourFits("90EE90FF", "", false, "#90ee90", ""));
  // A clear filament (alpha 00) is no black one, another hue is no fit, and a
  // filament that has a list of colours is no single colour.
  assert(!bambuddyColourFits("00000000", "", false, "000000", ""));
  assert(!bambuddyColourFits("111111", "", false, "000000", ""));
  assert(!bambuddyColourFits("000000", "000000,FFFFFF", false, "000000", ""));
  // Nothing to compare fits nothing.
  assert(!bambuddyColourFits(nullptr, nullptr, false, "000000", ""));
  assert(!bambuddyColourFits("", "", false, "000000", ""));
  assert(!bambuddyColourFits(nullptr, "", false, "000000", ""));
  assert(!bambuddyColourFits("", nullptr, false, "000000", ""));

  // A clear colour wanted: Spoolman keeps it as 00000000, a see-through one
  // as a hue with alpha 00; a hue with no alpha, or nothing, is not clear.
  assert(bambuddyColourFits("00000000", "", true, "", ""));
  assert(bambuddyColourFits("FFFFFF00", "", true, "", nullptr));
  assert(bambuddyColourFits("ffffff00", nullptr, true, "", ""));
  assert(!bambuddyColourFits("000000", "", true, "", ""));
  assert(!bambuddyColourFits("000000FF", "", true, "", ""));
  assert(!bambuddyColourFits("", "", true, "", ""));
  assert(!bambuddyColourFits(nullptr, nullptr, true, "", ""));

  // Colours side by side or along: the same list in the same order, for the
  // order is the direction of a gradient. The first colour rides along as the
  // input's color_hex does; the filament's own color_hex is not looked at.
  assert(bambuddyColourFits("", "9cdbd9,ffffff", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(bambuddyColourFits("", "9CDBD9,FFFFFF", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(bambuddyColourFits("", "#9CDBD9,#FFFFFF", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(bambuddyColourFits(nullptr, "9CDBD9,FFFFFF", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(!bambuddyColourFits("", "FFFFFF,9CDBD9", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(!bambuddyColourFits("", "9CDBD9", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(!bambuddyColourFits("9CDBD9", "9CDBD9", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(!bambuddyColourFits("", "", false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(!bambuddyColourFits("9CDBD9", nullptr, false, "9CDBD9", "9CDBD9,FFFFFF"));
  assert(!bambuddyColourFits("", "9CDBD9,FFFFFF,000000", false, "9CDBD9", "9CDBD9,FFFFFF"));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp),
                    str(root / "src" / "services" / "bambuddy_name.cpp"),
                    str(root / "src" / "services" / "text_util.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("bambuddy name rules: ok")
