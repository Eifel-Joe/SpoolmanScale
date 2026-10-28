"""Read the source tree without a compiler and check the colour as RGB on the label (w04, V43): a field of its own, LF_COLOR_RGB = 16 after every field there was, so that a saved layout keeps its meaning; on the label it is the line right after the colour, captioned "RGB" ("RVB"), on the spool's label and on the filament's; in the label editor its switch "Colour as RGB" comes right after the colour's; it counts as a fact line in every arrangement, a filament label may have it, and neither default turns it on. The texts are in all three languages and in the French file, the caption has its 16 px budget."""
import json
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer and the function finder of the convention ratchet: comments
# and string contents blanked, function bodies by brace matching. The script
# is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

LAYOUT_H = "services/label_layout.h"
LAYOUT = "services/label_layout.cpp"
FACTS = "services/label_facts.cpp"
PAGE = "web/pages/page_label.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def view_of(rel):
    """One file under src/, comments gone and string contents blanked."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8")).code


def body_of(rel, name):
    """The code between the braces of the one function called name, squeezed."""
    code = view_of(rel)
    bodies = [code[start + 1:end] for found, start, end in cc.find_functions(code) if found == name]
    assert len(bodies) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(bodies[0])


# 1. The field: appended, the last one before the count. The fields are bits
# of a saved mask, so the ones there were keep their numbers.
enum = re.search(r"enumLabelField:uint8_t\{(.*?)\};", squeeze(view_of(LAYOUT_H)))
assert enum, f"src/{LAYOUT_H}: no enum LabelField"
assert re.search(r"(^|,)LF_COLOR_RGB=16,LABEL_FIELD_COUNT,?$", enum.group(1)), \
    f"src/{LAYOUT_H}: LF_COLOR_RGB = 16 is not the field right before LABEL_FIELD_COUNT"

# 2. On the label: the RGB line follows the colour's, on both labels. The
# switch decides it, and the text is spoolColorRgbText()'s.
for fn in ("labelSpoolFacts", "labelFilamentFacts"):
    where = f"src/{FACTS}: {fn}()"
    body = body_of(FACTS, fn)
    adds = [m.start() for m in re.finditer(r"(?<![A-Za-z0-9_])addFact\(", body)]
    colour = [k for k, at in enumerate(adds) if "T(STR_LBL_L_COLOR)" in body[at:body.find(";", at)]]
    assert len(colour) == 1, f"{where}: the colour is not one fact"
    k = colour[0]
    assert k + 1 < len(adds) and "T(STR_LBL_L_RGB)" in body[adds[k + 1]:body.find(";", adds[k + 1])], \
        f"{where}: the fact right after the colour is not the RGB line (T(STR_LBL_L_RGB))"
    assert "LF_COLOR_RGB" in body[adds[k]:adds[k + 1]], f"{where}: the RGB line is not on the switch LF_COLOR_RGB"
    assert "spoolColorRgbText(" in body, f"{where}: the RGB line is not spoolColorRgbText()'s"

# 3. In the label editor: the switch right after the colour's.
rows = re.search(r"FIELD_ROWS\[\]=\{(.*?)\};", squeeze(view_of(PAGE)))
assert rows, f"src/{PAGE}: no FIELD_ROWS"
entries = re.findall(r"\{(\w+),(\w+)\}", rows.group(1))
fields = [f for f, _ in entries]
assert "LF_COLOR" in fields, f"src/{PAGE}: FIELD_ROWS has no LF_COLOR"
at = fields.index("LF_COLOR")
assert at + 1 < len(entries) and entries[at + 1] == ("LF_COLOR_RGB", "STR_LBL_F_COLOR_RGB"), \
    f"src/{PAGE}: FIELD_ROWS has not {{ LF_COLOR_RGB, STR_LBL_F_COLOR_RGB }} right after LF_COLOR"

# 4. The layouts: a fact line in every arrangement (not under a condition of
# the preset), a field the filament label has, on in neither default.
body = body_of(LAYOUT, "labelFactFields")
hits = [m.start() for m in re.finditer(r"LF_COLOR_RGB", body)]
assert hits, f"src/{LAYOUT}: labelFactFields() does not count LF_COLOR_RGB"
for pos in hits:
    depth = body[:pos].count("{") - body[:pos].count("}")
    start = max(body.rfind(";", 0, pos), body.rfind("{", 0, pos), body.rfind("}", 0, pos)) + 1
    assert depth == 0 and not body[start:].startswith("if("), \
        f"src/{LAYOUT}: labelFactFields() counts LF_COLOR_RGB for some arrangements only"
layout = view_of(LAYOUT)
for name, want in (("FILAMENT_FIELDS", True), ("DEFAULT_FIELDS", False), ("FILAMENT_DEFAULT_FIELDS", False)):
    m = re.search(r"\b" + name + r"\s*=\s*([^;]*);", layout)
    assert m, f"src/{LAYOUT}: no {name}"
    assert ("LF_COLOR_RGB" in m.group(1)) == want, \
        f"src/{LAYOUT}: {name} {'lacks' if want else 'has'} LF_COLOR_RGB"

# 5. The texts.
lang_h = (src / "lang.h").read_text(encoding="utf-8")
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
fr = {}
for line in (root / "tools" / "lang_fr.jsonl").read_text(encoding="utf-8").splitlines():
    if line.strip():
        row = json.loads(line)
        fr[row["id"]] = row
for sid, texts in (("STR_LBL_L_RGB", ("RGB", "RGB", "RVB")),
                   ("STR_LBL_F_COLOR_RGB", ("Farbe als RGB", "Colour as RGB", "Couleur en RVB"))):
    assert re.search(r"^\s*" + sid + r"\s*,", lang_h, re.M), f"src/lang.h: no {sid}"
    row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                    + sid + r"\b", lang_cpp)
    assert row and row.groups() == texts, f"src/lang.cpp: {sid} is not {texts}"
    assert sid in fr and (fr[sid].get("de"), fr[sid].get("en"), fr[sid].get("fr")) == texts, \
        f"tools/lang_fr.jsonl: {sid} is not {texts}"
# The caption is drawn on the label beside the colour's, in its font and to
# its width; it must fit, no dots.
budgets = (root / "tools" / "ui_budgets.tsv").read_text(encoding="utf-8")
colour = re.search(r"^STR_LBL_L_COLOR\t(\d+)\t(\d+)\tNEVER\t", budgets, re.M)
assert colour, "tools/ui_budgets.tsv: STR_LBL_L_COLOR lost its budget"
rgb = re.search(r"^STR_LBL_L_RGB\t(\d+)\t(\d+)\t(\S+)\t", budgets, re.M)
assert rgb and rgb.groups() == ("16", colour.group(2), "NEVER"), \
    f"tools/ui_budgets.tsv: STR_LBL_L_RGB has not the budget of STR_LBL_L_COLOR (16 px, {colour.group(2)}, NEVER)"

print("label rgb: ok")
