"""Measure five-digit spool ids the way LVGL does (tools/fit_check.py, the font tables the firmware links, rounded per glyph) and check every place that shows a spool id in a column of its own: the main screen, "More info", the AMS slot card and the unknown tag card keep the widest five digits whole without running into the next column, and the copy and link lists start the name behind the id that is there. The original widened the main screen to 54 px (o16, 546e478); a "0" and a "4" are 11 px each, so 40000 takes 55."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"
sys.path.insert(0, str(root / "tools"))
import fit_check  # noqa: E402

DIGITS = "0123456789"
widest = {d: fit_check.width(d, 16)[0] for d in DIGITS}
w_max = max(widest.values())
FIVE = fit_check.width(max(DIGITS, key=lambda d: widest[d]) * 5, 16)[0]
assert FIVE == 5 * w_max, f"five of the widest digit measure {FIVE} px, not 5 x {w_max}"
assert fit_check.width("40000", 16)[0] == FIVE, "40000 is one of the widest five-digit ids"
HASH_FIVE = fit_check.width("#" + "0" * 5, 16)[0]

problems = []


def need(cond, text):
    if not cond:
        problems.append(text)


def text_of(rel):
    path = src / rel
    if not path.is_file():
        problems.append(f"src/{rel}: file is missing")
        return ""
    return path.read_text(encoding="utf-8")


def squeeze(text):
    return re.sub(r"\s+", "", text)


# 1. The main screen: the id label from its x to "Material" at 112.
ms = text_of("ui/main_screen.cpp")
m_pos = re.search(r"lv_obj_set_pos\(lbl_spoolman_id,\s*(\d+),\s*69\)", ms)
m_w = re.search(r"lv_obj_set_width\(lbl_spoolman_id,\s*(\d+)\)", ms)
m_mat = re.search(r"lv_obj_set_pos\(lbl_mat_cap,\s*(\d+),", ms)
need(m_pos and m_w and m_mat, "src/ui/main_screen.cpp: position, width or the Material caption not found")
if m_pos and m_w and m_mat:
    x, w, mat = int(m_pos.group(1)), int(m_w.group(1)), int(m_mat.group(1))
    need(w >= FIVE, f"main screen: the id is {w} px wide, five digits take up to {FIVE}")
    need(x + w <= mat, f"main screen: the id ends at {x + w}, Material begins at {mat}")

# 2. "More info": the same column on the card.
mi = text_of("ui/more_info_screen.cpp")
i_pos = re.search(r"lv_obj_set_pos\(lbl_id,\s*(\d+),\s*76\)", mi)
i_w = re.search(r"lv_obj_set_size\(lbl_id,\s*(\d+),", mi)
i_mat = re.search(r"lv_obj_set_pos\(mi_mat_cap,\s*(\d+),", mi)
need(i_pos and i_w and i_mat, "src/ui/more_info_screen.cpp: position, width or the Material caption not found")
if i_pos and i_w and i_mat:
    x, w, mat = int(i_pos.group(1)), int(i_w.group(1)), int(i_mat.group(1))
    need(w >= FIVE, f"More info: the id is {w} px wide, five digits take up to {FIVE}")
    need(x + w <= mat, f"More info: the id ends at {x + w}, Material begins at {mat}")

# 3. The AMS slot card.
ad = text_of("ui/ams_detail_popup.cpp")
d = {n: int(re.search(rf"#define\s+{n}\s+(\d+)", ad).group(1)) for n in ("AMSD_ID_X", "AMSD_ID_W", "AMSD_MAT_X")}
need(squeeze("oneLine(id, AMSD_ID_W, UI_FONT_BODY);") in squeeze(ad), "src/ui/ams_detail_popup.cpp: the id is not bounded by AMSD_ID_W")
need(d["AMSD_ID_W"] >= FIVE, f"AMS slot card: the id is {d['AMSD_ID_W']} px wide, five digits take up to {FIVE}")
need(d["AMSD_ID_X"] + d["AMSD_ID_W"] <= d["AMSD_MAT_X"],
     f"AMS slot card: the id ends at {d['AMSD_ID_X'] + d['AMSD_ID_W']}, Material begins at {d['AMSD_MAT_X']}")

# 4. The unknown tag card: "#" and the id, right aligned, beside a name that
# ends UNK_ID_W before the row's end; 4 px between them as for "#1640".
us = text_of("ui/unknown_spool_popup.cpp")
unk = int(re.search(r"#define\s+UNK_ID_W\s+(\d+)", us).group(1))
need(unk >= HASH_FIVE + 4, f"unknown tag card: UNK_ID_W is {unk}, '#' and five digits take {HASH_FIVE} plus 4 px")

# 5. The copy and the link list: the id at the row's left, the name behind it.
# The name used to begin at 50 whatever the id, where five digits end at 61.
lr = text_of("ui/list_row_layout.h")
c = {}
for n in ("LIST_ROW_ID_X", "LIST_ROW_NAME_X", "LIST_ROW_ID_GAP", "LIST_ROW_TEXT_END"):
    found = re.search(rf"constexpr\s+int\s+{n}\s*=\s*(\d+);", lr)
    c[n] = int(found.group(1)) if found else 0


def name_x(id_w):
    """listRowNameX() as list_row_layout.h spells it."""
    return max(c["LIST_ROW_ID_X"] + id_w + c["LIST_ROW_ID_GAP"], c["LIST_ROW_NAME_X"])


need(squeeze("return x > LIST_ROW_NAME_X ? x : LIST_ROW_NAME_X;") in squeeze(lr) and
     squeeze("int x = LIST_ROW_ID_X + id_w + LIST_ROW_ID_GAP;") in squeeze(lr),
     "src/ui/list_row_layout.h: listRowNameX() is not spelled as this test reads it")
need(name_x(fit_check.width("99", 16)[0]) == c["LIST_ROW_NAME_X"], "lists: a two-digit id must leave the name where it was")
need(name_x(FIVE) >= c["LIST_ROW_ID_X"] + FIVE + c["LIST_ROW_ID_GAP"], "lists: five digits run into the name")
for rel in ("ui/copy_flow.cpp", "ui/spool_flow.cpp"):
    s = squeeze(text_of(rel))
    need(s.count(squeeze("const int name_x = listRowNameX(lv_txt_get_width(id_buf, strlen(id_buf), "
                         "&lv_font_montserrat_ext_16, 0, LV_TEXT_FLAG_NONE));")) == 1,
         f"src/{rel}: the name's place is not worked out from the id's width")
    need(s.count(squeeze("lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, name_x, 5);")) == 1 and
         s.count(squeeze("lv_obj_set_width(lbl_name, LIST_ROW_TEXT_END - name_x);")) == 1,
         f"src/{rel}: the name does not begin at name_x and end at LIST_ROW_TEXT_END")
    need(squeeze("lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 50, 5);") not in s,
         f"src/{rel}: a name still begins at 50 whatever the id")

if problems:
    for p in problems:
        print("FAIL", p)
    raise AssertionError(f"{len(problems)} place(s) where a five-digit spool id does not fit")
print(f"spool id width: ok (five digits up to {FIVE} px, '#' and five {HASH_FIVE} px)")
