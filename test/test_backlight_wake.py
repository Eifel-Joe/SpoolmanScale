"""Read the source tree without a compiler and check how the 7B's light comes back after "Screen off" (Punkteliste n61): the brightness goes to the expander before the light is switched on (so it does not flash at full) and once more after it, because the CH32V003 keeps showing the level it had when the light went off - the idle dim - and a level written while the light is off does not reach the panel. Before, the screen came back darker than set until the slider was touched."""
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

PANEL = "boards/waveshare_s3_lcd7b/panel.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    code = cc.Source(path.read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


on = body_of(PANEL, "panelBacklightOn")
light = squeeze("io_ch32v003::write(hw_pins::EXIO_BACKLIGHT, true);")
level = squeeze("panelSetBrightness(brightness);")
assert on.count(light) == 1, f"src/{PANEL}: panelBacklightOn() does not switch the light on exactly once"
assert on.find(level) >= 0 and on.find(level) < on.find(light), \
    f"src/{PANEL}: panelBacklightOn() does not set the level before the light comes on"
assert on.rfind(level) > on.find(light), \
    f"src/{PANEL}: panelBacklightOn() does not set the level again once the light is on"
assert on.endswith(level), f"src/{PANEL}: panelBacklightOn() does something after the second level"

print("7B light comes back at the set level: ok")
