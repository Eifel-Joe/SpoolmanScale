"""The product line in a filament name from a Bambu tag (o20, Niko11111 a7247a3, v0.8.7-beta.5). Bambu names 14 colours with the line in front ("Translucent Gray" under PETG Translucent), and the Spoolman name put the line in front a second time ("Translucent Translucent Gray"). Our own test in place of Niko's: his lives in test_tag_color_name.py, which belongs to a colour-name contribution we do not have. Compiles the rule from services/tag_create.h and reads the name builder in tag_create.cpp, which needs the Arduino core and has no host build."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

source = r"""
#include "services/tag_create.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  TagCreateInput in{};
  // The colour opens with the line: the line must not go in front again.
  snprintf(in.subtype, sizeof(in.subtype), "%s", "Translucent");
  snprintf(in.color_name_en, sizeof(in.color_name_en), "%s", "Translucent Gray");
  assert(tagCreateColorCarriesLine(in));
  snprintf(in.color_name_en, sizeof(in.color_name_en), "%s", "translucent teal");   // any case
  assert(tagCreateColorCarriesLine(in));
  snprintf(in.color_name_en, sizeof(in.color_name_en), "%s", "Translucent");
  assert(tagCreateColorCarriesLine(in));
  // The word has to end there, and a colour of its own stays apart from it.
  snprintf(in.color_name_en, sizeof(in.color_name_en), "%s", "Translucency Blue");
  assert(!tagCreateColorCarriesLine(in));
  snprintf(in.color_name_en, sizeof(in.color_name_en), "%s", "Jade White");
  assert(!tagCreateColorCarriesLine(in));
  // No line, or no colour: nothing is carried.
  in.color_name_en[0] = '\0';
  assert(!tagCreateColorCarriesLine(in));
  snprintf(in.color_name_en, sizeof(in.color_name_en), "%s", "Translucent Gray");
  in.subtype[0] = '\0';
  assert(!tagCreateColorCarriesLine(in));
  // Whole words, as before: "Tough Cyan" begins with "Tough", "Tough+ Cyan" not.
  assert(tagCreateStartsWithWord("Tough Cyan", "Tough"));
  assert(!tagCreateStartsWithWord("Tough+ Cyan", "Tough"));
  return 0;
}
"""


def squeeze(text):
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    code = cc.Source((src / rel).read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


# The name builder asks the rule before it puts the line in front.
name = body_of("services/tag_create.cpp", "tagCreateSpoolmanName")
assert "!tagCreateColorCarriesLine(in)" in name, \
    "src/services/tag_create.cpp: tagCreateSpoolmanName() puts the line in front of a colour that opens with it"

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(src),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("tag line name: ok")
