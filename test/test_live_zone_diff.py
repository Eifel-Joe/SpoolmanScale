"""Read the source tree without a compiler and check that the diff beside the netto on the main screen never runs past the line to TARE (f03): "Diff -1038 g" in Montserrat 20 is about 72 px wide, and its column from x = 344 ends at 400, 8 px before the divider at 408. Both diffs of the zone (the netto's and, with a bag, the one without it) are written through liveZoneSetDiff(), which takes the largest font from the layout's own down to 12 in which the text fits the column, and keeps the baseline.

The widths are read from the font files the firmware is built with (adv_w of each glyph, in 1/16 px), so the test knows without a panel that the layout's own font is too wide for four digits and that the smallest step holds the widest diff."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

ZONE = "ui/live_zone.cpp"
ZONE_H = "ui/live_zone.h"
LOOP = "app/app_loop.cpp"
DIFF_X = 344       # where both diffs start, live_zone.cpp
COLUMN_END = 400   # column 2 of zone 4, main_screen.cpp: x=218..400, divider at 408


def squeeze(text):
    return re.sub(r"\s+", "", text)


def code_of(rel):
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return squeeze(cc.Source(path.read_text(encoding="utf-8")).code)


def body_of(rel, name):
    path = src / rel
    code = cc.Source(path.read_text(encoding="utf-8")).code
    bodies = [code[start + 1:end] for found, start, end in cc.find_functions(code) if found == name]
    assert len(bodies) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(bodies[0])


def width(text, size):
    """The advance of text in lv_font_montserrat_ext_<size>, in px, without kerning."""
    font = (src / "fonts" / f"lv_font_montserrat_ext_{size}.c").read_text(encoding="utf-8")
    adv = [int(x) for x in re.findall(r"\.adv_w = (\d+)", font[font.index("glyph_dsc[] = {"):])]
    # Glyph 1 is U+0020; the range runs without a gap to U+007F.
    return sum(adv[ord(c) - 0x20 + 1] for c in text) / 16


def main():
    room = COLUMN_END - DIFF_X
    zone = code_of(ZONE)

    # The finding itself: the layout's fonts are too wide for a four-digit diff.
    assert width("-1038 g", 20) > room and width("-1038 g", 16) > room, "the finding no longer holds"

    # The column and the steps in live_zone.cpp.
    assert f"#defineDIFF_X{DIFF_X}" in zone, f"src/{ZONE}: DIFF_X is not {DIFF_X}"
    assert f"#defineDIFF_RIGHT{COLUMN_END}" in zone, f"src/{ZONE}: DIFF_RIGHT is not the column's end {COLUMN_END}"
    steps = re.search(r"DIFF_STEPS\[\]=\{([^}]*)\}", zone)
    assert steps, f"src/{ZONE}: no DIFF_STEPS[]"
    sizes = [int(s) for s in re.findall(r"&lv_font_montserrat_ext_(\d+)", steps.group(1))]
    assert sizes == sorted(sizes, reverse=True) and sizes[0] >= 20 and sizes[-1] == 12, \
        f"src/{ZONE}: DIFF_STEPS {sizes} does not run from the layout's 20 down to 12"
    for text in ("-9999 g", "+9999 g"):
        assert width(text, sizes[-1]) <= room, f"{text} does not fit {room} px even in {sizes[-1]}"

    # Both diffs are placed at DIFF_X and set through liveZoneSetDiff().
    for build, labels in (("buildWithBag", ("lbl_raw_info", "lbl_bag_sm_diff")), ("buildBare", ("lbl_raw_info",))):
        body = body_of(ZONE, build)
        for label in labels:
            assert f"{label}=diff(" in body, f"src/{ZONE}: {build}() places {label} by hand, not through diff()"
    setter = body_of(ZONE, "liveZoneSetDiff")
    for needle in ("lv_txt_get_size(", "DIFF_RIGHT-DIFF_X", "lv_obj_set_style_text_font(", "lv_obj_set_y("):
        assert needle in setter, f"src/{ZONE}: liveZoneSetDiff() lacks {needle}"
    assert "voidliveZoneSetDiff(lv_obj_t*lbl,constchar*text);" in code_of(ZONE_H), f"src/{ZONE_H}: no liveZoneSetDiff()"

    loop = code_of(LOOP)
    for label in ("lbl_raw_info", "lbl_bag_sm_diff"):
        assert f"liveZoneSetDiff({label}," in loop, f"src/{LOOP}: {label} is not set through liveZoneSetDiff()"
        written = re.findall(rf"lv_label_set_text\({label},([^;]*)\);", loop)
        assert all(w == '""' for w in written), f"src/{LOOP}: {label} gets a text other than empty past liveZoneSetDiff(): {written}"
    print("live zone diff: ok")


main()
