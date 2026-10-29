"""Read the source tree without a compiler and check the sizes the label's fact lines are offered (V45): one candidate list per head, 203 and 300 dpi, the two-line styles as they were and after them the one-line styles in every even print size down to just above the small line (300 dpi: 36 to 26, 203 dpi: 24 to 18); planLarger() leaves the choice to labelFactPick(), compares print sizes and no raw line heights; the log names the size; the one-line sizes are fonts of their own, PRINTED_18 to PRINTED_36, copies that print as themselves on any head, so the table of the finer head and the stroke of boldFor() stay as they are."""
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

RENDER = "services/label_render.cpp"
DRAW_H = "services/label_draw.h"
DRAW = "services/label_draw.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def source_of(rel):
    """One file under src/ in the aligned views of the tokenizer."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def view_of(rel):
    """One file under src/, comments gone and string contents blanked."""
    return source_of(rel).code


def body_of(rel, name, literals=False):
    """The code between the braces of the one function called name; with
    literals the string contents are kept (comments are gone in both)."""
    s = source_of(rel)
    spans = [(a, b) for found, a, b in cc.find_functions(s.code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    a, b = spans[0]
    return (s.code_str if literals else s.code)[a + 1:b]


def ext(n):
    return f"&lv_font_montserrat_ext_{n}"


def printed(n):
    return f"&PRINTED_{n}"


# 1. The candidate lists, one per head: value font, caption font, two_lines,
# print size of the value font. The two-line rows are the ones of today in
# today's order; behind them the one-line rows in every even print size from
# the largest one-line style down to just above the small line; the last row
# is the small lines once more, which only a label without a code may take.
# On the 300 dpi head a 203 dpi font prints in the next size of FONTS_300,
# which is why its two-line rows carry 40, 36 and 30 and why the one-line
# rows are fonts that print as themselves.
EXPECTED = {
    "FACT_STYLES_203": [
        (ext(28), ext(16), "true", "28"),
        (ext(24), ext(14), "true", "24"),
        (ext(20), ext(14), "true", "20"),
        (printed(24), ext(14), "false", "24"),
        (printed(22), ext(14), "false", "22"),
        (printed(20), ext(14), "false", "20"),
        (printed(18), ext(14), "false", "18"),
        (ext(16), ext(14), "false", "16"),
    ],
    "FACT_STYLES_300": [
        (ext(28), ext(16), "true", "40"),
        (ext(24), ext(14), "true", "36"),
        (ext(20), ext(14), "true", "30"),
        (printed(36), ext(14), "false", "36"),
        (printed(34), ext(14), "false", "34"),
        (printed(32), ext(14), "false", "32"),
        (printed(30), ext(14), "false", "30"),
        (printed(28), ext(14), "false", "28"),
        (printed(26), ext(14), "false", "26"),
        (ext(16), ext(14), "false", "24"),
    ],
}
render_src = source_of(RENDER)
render = render_src.code
flat = squeeze(render)
for name, want in EXPECTED.items():
    table = re.search(r"FactStyle" + name + r"\[\d*\]=\{(.*?)\};", flat)
    assert table, f"src/{RENDER}: no table {name} of FactStyle"
    got = [tuple(row.rstrip(",").split(",")) for row in re.findall(r"\{([^{}]*)\}", table.group(1))]
    assert got == want, \
        f"src/{RENDER}: {name} has not the rows of the ruling (value, caption, two_lines, px):\n" \
        f"  want {want}\n  got  {got}"

# 2. The row carries the print size of its value font as the fourth field.
assert re.search(r"struct\s+FactStyle\s*\{\s*const\s+lv_font_t\s*\*\s*value\s*;\s*const\s+lv_font_t\s*\*\s*caption\s*;"
                 r"\s*bool\s+two_lines\s*;\s*(?:[A-Za-z_]\w*\s+)+px\s*;\s*\}\s*;", render), \
    f"src/{RENDER}: struct FactStyle is not {{ value, caption, two_lines, px }}"

# 3. One list per head: the table of the old shape, one for both heads, is gone.
assert not re.search(r"\bFACT_STYLES\b", render), \
    f"src/{RENDER}: the old table FACT_STYLES (one list for both heads) is still there"

# 4. The renderer takes the list of the head it draws for: the dpi that
# setDpi() was given, 203 the list of 203 dpi, anything else the other.
assert re.search(r"\bdpi\s*\(\s*\)", render), f"src/{RENDER}: the head's dpi() is never asked"
for name in EXPECTED:
    assert len(re.findall(r"\b" + name + r"\b", render)) >= 2, \
        f"src/{RENDER}: {name} is defined but never taken"

# 5. The rule: planLarger() leaves the choice to labelFactPick() and compares
# the sizes as they print, never the raw line heights of the 203 dpi fonts
# (on the finer head those are not the sizes that print). The candidates
# live in fixed arrays: no heap.
plan = body_of(RENDER, "planLarger")
assert "labelFactPick(" in squeeze(plan), f"src/{RENDER}: planLarger() does not ask labelFactPick("
assert not re.search(r"\bline_height\b", plan), \
    f"src/{RENDER}: planLarger() still reads a raw line_height; lineHeight() is the print size"
assert not re.search(r"\b(?:malloc|calloc|realloc|heap_caps_\w+|new|vector)\b", plan), \
    f"src/{RENDER}: planLarger() takes its candidates from the heap; they live in fixed arrays"

# 6. The log names the size that was chosen: "<px> px lines" for one line
# larger, "two lines of <px> px" for two lines, "small lines" as before.
# The text is made in drawBody(), or in the function that writes the log
# line it feeds.
log_fns = [render_src.code_str[a + 1:b] for _, a, b in cc.find_functions(render)
           if "facts in" in render_src.code_str[a + 1:b]]
assert len(log_fns) == 1, f"src/{RENDER}: the label log line `facts in` is not written in exactly one function"
region = body_of(RENDER, "drawBody", literals=True) + log_fns[0]
for text in ("px lines", "two lines of", "small lines"):
    assert text in region, f'src/{RENDER}: drawBody() and the log do not hold the text "{text}"'
assert re.search(r"(?:\.|->)px\b", region), \
    f"src/{RENDER}: the size in the log is not the px of the chosen row"

# 7. The one-line sizes: PRINTED_18 to PRINTED_36, every even size, declared
# in label_draw.h inside namespace labeldraw and defined in label_draw.cpp
# as a copy of the font of that size. A copy has an address of its own, is
# no key of FONTS_300, and so sized() hands it back as it is.
sizes = range(18, 37, 2)
header = view_of(DRAW_H)
h_space = header.find("namespace labeldraw")
assert h_space >= 0, f"src/{DRAW_H}: no namespace labeldraw"
declared = set()
for m in re.finditer(r"\bextern\s+const\s+lv_font_t\s+([^;()]*);", header[h_space:]):
    declared.update(n.strip() for n in m.group(1).split(","))
for n in sizes:
    assert f"PRINTED_{n}" in declared, \
        f"src/{DRAW_H}: PRINTED_{n} is not declared (extern const lv_font_t) inside namespace labeldraw"
draw = squeeze(view_of(DRAW))
d_space = draw.find("namespacelabeldraw{")
assert d_space >= 0, f"src/{DRAW}: no namespace labeldraw"
defined = re.findall(r"(?:extern)?constlv_font_t(PRINTED_\d+)=(lv_font_montserrat_ext_\d+);", draw[d_space:])
for n in sizes:
    assert defined.count((f"PRINTED_{n}", f"lv_font_montserrat_ext_{n}")) == 1, \
        f"src/{DRAW}: PRINTED_{n} is not defined once as = lv_font_montserrat_ext_{n}; inside namespace labeldraw"

# 8. The finer head's table stays as it is: the seven pairs of today. No
# PRINTED font is a key of it, or sized() would not hand it back as it is.
steps = re.search(r"FontStepFONTS_300\[\]=\{(.*?)\};", draw)
assert steps, f"src/{DRAW}: no table FONTS_300"
pairs = [(f"lv_font_montserrat_ext_{a}", f"lv_font_montserrat_ext_{b}")
         for a, b in ((14, 20), (16, 24), (20, 30), (22, 32), (24, 36), (28, 40), (36, 48))]
assert re.findall(r"\{&(\w+),&(\w+)\}", steps.group(1)) == pairs, \
    f"src/{DRAW}: FONTS_300 is not the seven pairs of today (14 to 20, 16 to 24, 20 to 30, 22 to 32, 24 to 36, 28 to 40, 36 to 48)"

# 9. The stroke is by print size: a font gets the bold stroke when it prints
# no larger than the 24 does on this head. Every font asked today is a key of
# FONTS_300, and that grows with the size, so every answer of today stays.
bold = squeeze(body_of(DRAW, "boldFor"))
assert "returnsized(font)->line_height>sized(&lv_font_montserrat_ext_24)->line_height?0:dots(LR_BOLD_DOTS);" in bold, \
    f"src/{DRAW}: boldFor() does not compare the print sizes, sized(font)->line_height against sized(&lv_font_montserrat_ext_24)->line_height"

# 10. The getter the renderer asks for the head's dpi: declared, defined,
# and what setDpi() was given.
assert re.search(r"\buint16_t\s+dpi\s*\(\s*(?:void)?\s*\)\s*;", header[h_space:]), \
    f"src/{DRAW_H}: no uint16_t dpi(); inside namespace labeldraw"
assert "s_dpi" in body_of(DRAW, "dpi"), f"src/{DRAW}: dpi() does not return the dpi setDpi() was given (s_dpi)"

print("test_label_fact_styles: ok")
