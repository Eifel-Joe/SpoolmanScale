"""Read the source tree without a compiler and check that every keyboard on the panel sets its own keys (w02, V43): LVGL keeps the maps of lv_keyboard_set_map() in one table for all keyboards, so the WiFi password's layout (no keyboard key, which sends the cancel) stayed on every keyboard opened after it. Each lv_keyboard_create() is followed by keyMapsApply() for that keyboard, the WiFi password asks for its own layout and the others for the text one; lv_keyboard_set_map() is called in ui/key_maps.cpp alone, which holds the control keys of the tables to LVGL's symbols with a static_assert each."""
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

MAPS_H = "ui/key_maps.h"
MAPS = "ui/key_maps.cpp"
# The keyboards there are, and the layout each asks for.
KEYBOARDS = {"ui/sort_entry_keys.cpp": "false", "ui/spool_type_name.cpp": "false", "ui/wifi_pass_screen.cpp": "true"}


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def rel_of(path):
    return str(path.relative_to(src)).replace("\\", "/")


# Only the files that name a keyboard at all are read as code (the fonts are
# large and name none).
views = {}
for path in sorted(p for p in src.rglob("*") if p.suffix in (".cpp", ".h", ".c")):
    text = path.read_text(encoding="utf-8", errors="replace")
    if "lv_keyboard" in text or "keyMapsApply" in text:
        views[rel_of(path)] = cc.Source(text).code

# 1. Every keyboard sets its keys, right after it is made, in the function
# that makes it: the layout of the language, or the WiFi password's.
asked = {}
for rel, code in views.items():
    for name, start, end in cc.find_functions(code):
        body = squeeze(code[start + 1:end])
        for m in re.finditer(r"lv_keyboard_create\(", body):
            where = f"src/{rel}: {name}()"
            made = re.search(r"(\w+)=$", body[:m.start()])
            assert made, f"{where}: the keyboard of lv_keyboard_create() is kept in no variable"
            kb = made.group(1)
            apply = re.search(r"keyMapsApply\(" + kb + r",(\w+)\)", body[m.end():])
            assert apply, f"{where}: the keyboard {kb} does not get its keys from keyMapsApply() after it is made"
            asked.setdefault(rel, []).append(apply.group(1))
for rel, wifi in KEYBOARDS.items():
    assert asked.get(rel) == [wifi], f"src/{rel}: the keyboard does not call keyMapsApply(..., {wifi})"

# 2. LVGL's table is set in one place.
setters = sorted(rel for rel, code in views.items() if "lv_keyboard_set_map(" in squeeze(code))
assert setters == [MAPS], f"src: lv_keyboard_set_map() is called in {setters}, not in src/{MAPS} alone"

# 3. The part the screens share, and the symbols of the tables held to the
# build's LVGL: one static_assert for each control key.
assert re.search(r"voidkeyMapsApply\(lv_obj_t\*\w*,bool\w*\);", squeeze(views.get(MAPS_H, ""))), \
    f"src/{MAPS_H}: no 'void keyMapsApply(lv_obj_t* kb, bool wifi);'"
assert MAPS in views, f"src/{MAPS}: file is missing"
maps = squeeze(views[MAPS])
asserts = []
for m in re.finditer(r"static_assert\(", maps):
    depth = 0
    for k in range(m.end() - 1, len(maps)):
        depth += {"(": 1, ")": -1}.get(maps[k], 0)
        if depth == 0:
            asserts.append(maps[m.end():k])
            break
for key in ("BACKSPACE", "NEW_LINE", "KEYBOARD", "LEFT", "RIGHT", "OK"):
    pair = [a for a in asserts if re.search(r"(?<![A-Za-z0-9_])KEY_SYM_" + key + r"(?![A-Za-z0-9_])", a)
            and re.search(r"(?<![A-Za-z0-9_])LV_SYMBOL_" + key + r"(?![A-Za-z0-9_])", a)]
    assert pair, f"src/{MAPS}: no static_assert holds KEY_SYM_{key} to LV_SYMBOL_{key}"
assert "constexpr" in maps, f"src/{MAPS}: the symbols are not compared by a constexpr function"

print("keyboards: ok")
