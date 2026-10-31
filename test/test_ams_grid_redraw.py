"""Read ui/ams_view.cpp without a compiler and check that the grid is always laid on an empty body: the weights that come in behind the first frame redraw the tiles, and a redraw that laid them over the ones already there held both sets in the pool, one more with every result. The redraw of the weights keeps where the user had scrolled to, because emptying the body puts LVGL back at the top. The checks name the clean and the scroll calls, not the order of the file, so the test holds when the redraw moves into a helper."""
import re

import klipper_src as ks

VIEW = "ui/ams_view.cpp"
GRID = "layoutGrid"
TICK = "handleAmsViewDeferredActions"
CLEAN = "lv_obj_clean(s_body)"

ks.assert_ci_step("test_ams_grid_redraw.py")

source = ks.source_of(VIEW)
bodies = {}
for name, a, b in ks.cc.find_functions(source.code):
    bodies[name] = ks.collapse(source.code[a + 1:b])

problems = []


def need(ok, text):
    if not ok:
        problems.append(f"src/{VIEW}: {text}")


def calls_at(body, name):
    return [m.start() for m in re.finditer(rf"\b{re.escape(name)}\s*\(", body)]


def block_after(text, at):
    """The text of the braces that follow the 'if (' at position at."""
    depth, i = 1, at + 4
    while i < len(text) and depth:
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        i += 1
    start = text.find("{", i)
    if start < 0:
        return ""
    depth, j = 1, start + 1
    while j < len(text) and depth:
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        j += 1
    return text[start:j]


need(GRID in bodies, f"{GRID}() is not defined")

# 1. Whoever lays the grid empties the body first, in its own text. Not through
#    the functions it calls: the loop's tick reaches fetchAndDraw(), which
#    cleans, on another branch, and that does not empty the body for the
#    weights redraw further down.
callers = [fn for fn, body in bodies.items() if fn != GRID and calls_at(body, GRID)]
need(callers, f"nothing calls {GRID}()")
for fn in callers:
    body = bodies[fn]
    for at in calls_at(body, GRID):
        need(CLEAN in body[:at], f"{fn}() calls {GRID}() without {CLEAN} before it")

# 2. The weights redraw in the tick keeps the scroll position: taken before the
#    clean, put back after the grid, with the layout run in between (before it
#    the content height is zero and LVGL clamps the offset away).
tick = bodies.get(TICK, "")
need(tick, f"{TICK}() is not defined")
cond = re.search(r"if \(\s*s_weights_redraw\b", tick)
need(cond, f"{TICK}() has no 'if (s_weights_redraw ...)' block")
if cond:
    block = block_after(tick, cond.start())
    drawers = [(fn, bodies[fn]) for fn in callers if fn != TICK and calls_at(block, fn)]
    if calls_at(block, GRID):
        drawers.append((f"the s_weights_redraw block of {TICK}()", block))
    need(drawers, f"the s_weights_redraw block of {TICK}() does not draw the grid")
    for who, text in drawers:
        clean_at = text.find(CLEAN)
        grid_at = text.find(GRID + "(")
        taken = re.search(r"lv_obj_get_scroll_y\s*\(\s*s_body\s*\)", text)
        need(taken and 0 <= clean_at and taken.start() < clean_at,
             f"{who} does not take lv_obj_get_scroll_y(s_body) before {CLEAN}")
        after = text[grid_at:] if grid_at >= 0 else ""
        layout = after.find("lv_obj_update_layout(s_body)")
        back = re.search(r"lv_obj_scroll_to_y\s*\(\s*s_body\s*,", after)
        need(layout >= 0, f"{who} does not run lv_obj_update_layout(s_body) after {GRID}()")
        need(back and layout < back.start(),
             f"{who} does not scroll back with lv_obj_scroll_to_y(s_body, ...) after the layout")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_ams_grid_redraw: ok")
