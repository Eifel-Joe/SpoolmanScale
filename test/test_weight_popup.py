"""Read the source tree without a compiler and check the weight popup (ui/weight_popup.cpp) where the fork adds to the original's: the bag switch keeps its state across the parts chooser, which the chip opens and which opens the popup again with a fresh reading, while every other open starts with the switch off as the original has it; and the first line, narrowed beside the parts chip, has the budget of the width left there, and the warning of a spool heavier than stored fits it in every language with every backend's name."""
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

WP = "ui/weight_popup.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def code_of(rel):
    """One file under src/ as the ratchet reads it, blanks gone."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return squeeze(cc.Source(path.read_text(encoding="utf-8")).code)


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    code = cc.Source((src / rel).read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def once(body, needle, where):
    """The needle is in body exactly once."""
    assert body.count(squeeze(needle)) == 1, f"{where}: '{needle}' is not there exactly once"


# 1. The bag switch and the parts chooser. The chip closes the popup and opens
# the chooser, which opens the popup again on its way out. Through
# showWeightPopup() that was a fresh open, which turns the switch off: bag on,
# chip, back, and the large button carried the bag weight again without a
# word. The way back is a function of its own that reads the pad again (the
# new parts come off) and keeps the switch, for the spool it was opened for.
wp = code_of(WP)
where = f"src/{WP}: wpBuildPartsChip()"
chip = body_of(WP, "wpBuildPartsChip")
once(chip, "s_parts_for = sm_id;", where)
once(chip, "showExtrasChooser(wpReopenAfterParts);", where)
assert squeeze("showExtrasChooser(showWeightPopup)") not in wp, \
    f"src/{WP}: the chooser still comes back through showWeightPopup(), which turns the bag switch off"

where = f"src/{WP}: wpReopenAfterParts()"
back = body_of(WP, "wpReopenAfterParts")
assert back.startswith(squeeze("if (sm_id != s_parts_for) { showWeightPopup(); return; }")), \
    f"{where}: another spool on the screen must get a fresh open, found '{back}'"
assert back.endswith(squeeze("s_gross_g = spoolExtrasBareReading(); wpOpenLevel1(false);")), \
    f"{where}: must read the pad again and open without a fresh start, found '{back}'"
assert not re.search(r"s_bag_on=(?!=)", back), f"{where}: must leave the bag switch as it was"

# Every other open is fresh, as the original has it: the reading and the
# switch off.
where = f"src/{WP}: wpOpenLevel1()"
once(body_of(WP, "wpOpenLevel1"), "if (fresh) { s_gross_g = spoolExtrasBareReading(); s_bag_on = false; }", where)
once(body_of(WP, "showWeightPopup"), "wpOpenLevel1(true);", f"src/{WP}: showWeightPopup()")

# 2. The first line beside the parts chip. With the chip (always there once
# parts are set up, as on a board whose holder is a default part) the line
# gives up the chip and a gap, and the text that warns of a spool heavier than
# stored had a budget of the whole width: it ran into dots before its two
# questions. Its budget is the width that is left, and in every language it
# fits there with every backend's name in place of %s, measured as LVGL does
# (tools/fit_check.py, the same tables the firmware links).
raw = (src / WP).read_text(encoding="utf-8")
define = {name: int(re.search(rf"#define\s+{name}\s+(\d+)\b", raw).group(1))
          for name in ("WP_INNER_W", "WP_CHIP_W", "WP_GAP")}
line_w = define["WP_INNER_W"] - define["WP_CHIP_W"] - define["WP_GAP"]
where = f"src/{WP}: wpBuildPartsChip()"
assert chip.endswith(squeeze("return WP_CHIP_W + WP_GAP;")), f"{where}: the chip no longer gives up WP_CHIP_W + WP_GAP"
where = f"src/{WP}: wpBuildHeaderAndPrimary()"
head = body_of(WP, "wpBuildHeaderAndPrimary")
once(head, "s_v.line1 = wpLabel(box, \"\", UI_FONT_SMALL, UI_COL_INK_SOFT);", where)
once(head, "lv_obj_set_width(s_v.line1, WP_INNER_W - chip);", where)
assert re.search(r"#define\s+UI_FONT_SMALL\s+\(&lv_font_montserrat_ext_14\)",
                 (src / "ui/theme.h").read_text(encoding="utf-8")), "src/ui/theme.h: UI_FONT_SMALL is no longer the 14 px font"

sys.path.insert(0, str(root / "tools"))
import fit_check  # noqa: E402

rows = [b for b in fit_check.read_budgets() if b["id"] == "STR_WP_RISE_LINE"]
assert rows, "tools/ui_budgets.tsv: STR_WP_RISE_LINE has no budget"
for b in rows:
    assert (b["font"], b["budget"], b["mode"]) == (14, line_w, "NEVER"), \
        f"tools/ui_budgets.tsv: STR_WP_RISE_LINE is budgeted f{b['font']} {b['budget']} px {b['mode']}, " \
        f"the line beside the chip is f14 {line_w} px NEVER"
names = re.findall(r'return\s+"([^"]+)";', re.search(r"backendModeName\(BackendMode mode\)\s*\{(.*?)\n\}",
                   (src / "services/backend.cpp").read_text(encoding="utf-8"), re.S).group(1))
assert len(names) == 3, f"src/services/backend.cpp: backendModeName() names {names}, expected three backends"
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*STR_WP_RISE_LINE\b',
                lang_cpp)
assert row, "src/lang.cpp: the row of STR_WP_RISE_LINE is not there"
for lang_name, text in zip(("German", "English", "French"), row.groups()):
    assert text.count("%s") == 1, f"src/lang.cpp: STR_WP_RISE_LINE ({lang_name}) must name the backend once"
    for name in names:
        shown = text.replace("%s", name)
        w, missing = fit_check.width(shown, 14)
        assert not missing and w <= line_w, \
            f"src/lang.cpp: STR_WP_RISE_LINE ({lang_name}) '{shown}' is {w} px at f14, the line beside the chip holds {line_w}"

print("weight popup: ok")
