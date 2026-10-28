"""Read the source tree without a compiler and check the weight popup (ui/weight_popup.cpp) where the fork adds to the original's: the bag switch keeps its state across the parts chooser, which the chip opens and which opens the popup again with a fresh reading, while every other open starts with the switch off as the original has it."""
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

print("weight popup: ok")
