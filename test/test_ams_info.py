"""Read the source tree without a compiler and check "into AMS" under "more info" (X, V35): the header of the page builds the button only when amsInfoOffered() says so, its tap raises a flag and closes the page, the loop asks again before it opens the bay picker for the spool on the screen, and a picker that a tap opened and that is closed without an answer is not followed by the location question (the tap was the question, not a removal)."""
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


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def code_of(rel):
    """The code of one file under src/ (comments and string contents blanked)."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8")).code


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    code = code_of(rel)
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def block_after(body, marker, where):
    """The text between the braces that open right after marker (found in body,
    which has no blanks), braces counted. Returns (block, rest of body without it)."""
    at = body.find(marker)
    assert at >= 0, f"{where}: '{marker}' is not there"
    open_at = at + len(marker) - 1
    assert body[open_at] == "{", f"{where}: '{marker}' does not end in an opening brace"
    depth = 0
    for i in range(open_at, len(body)):
        depth += body[i] == "{"
        depth -= body[i] == "}"
        if depth == 0:
            return body[open_at + 1:i], body[:at] + body[i + 1:]
    raise AssertionError(f"{where}: the block after '{marker}' is not closed")


def before(body, first, second, where):
    """first is in body and stands before the first appearance of second."""
    a, b = body.find(squeeze(first)), body.find(squeeze(second))
    assert a >= 0, f"{where}: '{first}' is not there"
    assert b >= 0, f"{where}: '{second}' is not there"
    assert a < b, f"{where}: '{first}' stands after '{second}'"


# The one question the page and the loop both put to the rule, argument for
# argument: the server can assign a bay, an AMS is there, a spool is on the
# screen that is not archived and has a number. Written out here so that a
# change of one argument, or an `||` or a second condition around the call,
# does not pass.
OFFER = squeeze("amsInfoOffered(backendCanAssignAmsSlot(), amsPresenceHasAms(), sm_found, sm_archived, sm_id)")

# 1. The page. The button sits in the header, left, and exists only inside the
# one `if` that asks the rule - not for a spool the rule turns down. Its tap
# raises the flag and closes the page, the picker itself opens from the loop,
# since the page's callback must not do it. The page is closed through
# releaseScreen() (hidden at once, deleted a timer pass later): the project's
# rules forbid lv_obj_del() inside a callback, and the X button's direct delete
# is one of the old cases the ratchet in scripts/check_conventions.py counts.
INFO = "ui/more_info_screen.cpp"
where = f"src/{INFO}: buildMoreInfoScreen()"
body = body_of(INFO, "buildMoreInfoScreen")
assert body.count("amsInfoOffered(") == 1, \
    f"{where}: amsInfoOffered() is called {body.count('amsInfoOffered(')} times, expected 1 (the build; the loop asks again)"
guard = "if(" + OFFER + "){"
block, rest = block_after(body, guard, where)
before(body, "lv_obj_t*hdr=lv_obj_create(box);", guard, where)
before(body, guard, "lv_obj_t*btn_x=lv_btn_create(hdr);", where)

made = re.search(r"lv_obj_t\*(\w+)=lv_btn_create\(hdr\);", block)
assert made, f"{where}: the button is not made as a child of hdr inside the 'if' on amsInfoOffered()"
btn = made.group(1)
for needle in (f"lv_obj_set_size({btn},116,34);",
               f"lv_obj_set_pos({btn},10,9);",
               f"lv_obj_set_style_bg_color({btn},lv_color_hex(UI_COL_ROW),0);",
               f"lv_obj_set_style_bg_color({btn},lv_color_hex(UI_COL_ROW_PRESS_FILL),LV_STATE_PRESSED);",
               f"lv_obj_set_style_border_color({btn},lv_color_hex(UI_COL_ACCENT),0);",
               f"lv_obj_set_style_border_width({btn},1,0);",
               f"lv_obj_set_style_radius({btn},UI_RADIUS_BTN,0);",
               f"lv_obj_set_style_shadow_width({btn},0,0);"):
    assert needle in block, f"{where}: the button does not have {needle} (it looks like 'Etikett drucken', at x 10 y 9)"
assert "lv_label_set_text(" in block and "T(STR_AMS_MAIN_ASSIGN)" in block, \
    f"{where}: the button is not captioned T(STR_AMS_MAIN_ASSIGN)"
assert "UI_FONT_SMALL" in block, f"{where}: the caption is not set in UI_FONT_SMALL like 'Etikett drucken'"
# What belongs to the button stays in its block: nothing outside offers it.
for needle in ("STR_AMS_MAIN_ASSIGN", "ams_info_assign_pending"):
    assert needle not in rest, f"{where}: {needle} appears outside the 'if' on amsInfoOffered()"

handler, after = block_after(block, f"lv_obj_add_event_cb({btn},[](lv_event_t*e){{", f"{where}: the button's click handler")
assert "ams_info_assign_pending=true;" in handler, f"{where}: the tap does not raise ams_info_assign_pending"
assert "amsPickOpenFor(" not in handler and "backendAssignAmsSlot(" not in handler, \
    f"{where}: the tap opens the picker itself, on the page's callback; it belongs to the loop"
assert "logSD(" in handler, f"{where}: the tap lost its log line"
close = "releaseScreen(&scr_more_info);"
assert close in handler, f"{where}: the tap does not close the page through releaseScreen(&scr_more_info)"
assert "lv_obj_del(" not in handler, f"{where}: the tap deletes an object inside a callback (use releaseScreen())"
# The page is gone after the release; nothing may touch it (or the button) later.
assert handler.endswith(close), f"{where}: the tap does something after it closed the page"
before(handler, "ams_info_assign_pending=true;", close, f"{where}: the tap")
assert ",LV_EVENT_CLICKED," in after, f"{where}: the handler is not registered for LV_EVENT_CLICKED"

# The flag is declared and defined next to ams_main_assign_pending.
header = (src / "app/deferred_actions.h").read_text(encoding="utf-8")
assert re.search(r"extern\s+bool\s+ams_info_assign_pending\s*;", header), \
    "src/app/deferred_actions.h: ams_info_assign_pending is not declared"
defined = (src / "app/deferred_actions.cpp").read_text(encoding="utf-8")
assert re.search(r"^bool\s+ams_info_assign_pending\s*=\s*false\s*;", defined, re.M), \
    "src/app/deferred_actions.cpp: ams_info_assign_pending is not defined as false"

# 2. The loop. The flag is cleared first, then the spool is asked about again
# (it can have left the screen, an archive can have come in, between the tap
# and this pass), and only then the picker opens - for the spool on the screen.
LOOP = "app/app_loop.cpp"
where = f"src/{LOOP}: appLoop()"
body = body_of(LOOP, "appLoop")
flag, rest = block_after(body, "if(ams_info_assign_pending){", where)
assert flag.startswith("ams_info_assign_pending=false;"), f"{where}: the flag is not cleared first"
assert "amsInfoAssignRun();" in flag, f"{where}: the pass does not call amsInfoAssignRun()"
assert "amsInfoAssignRun(" not in rest, f"{where}: amsInfoAssignRun() is called from somewhere else than the flag's pass"
assert "amsPickOpenFor(" not in body, f"{where}: appLoop() opens the picker itself, past the second question"

where = f"src/{LOOP}: amsInfoAssignRun()"
body = body_of(LOOP, "amsInfoAssignRun")
assert body.count("amsInfoOffered(") == 1, f"{where}: amsInfoOffered() is called {body.count('amsInfoOffered(')} times, expected 1"
dropped, rest = block_after(body, "if(!" + OFFER + "){", where)
assert "logSD" in dropped, f"{where}: a spool that is no longer offered is dropped without a log line"
assert "return;" in dropped, f"{where}: a spool that is no longer offered does not stop the pass"
assert "amsPickOpenFor(" not in dropped, f"{where}: the picker opens in the branch of the spool that is no longer offered"
assert rest.count("amsPickOpenFor(sm_id,sm_filament_name);") == 1, \
    f"{where}: the picker does not open for sm_id and sm_filament_name after the question"
# The button without a load cell is the one that was there, left alone.
body = body_of(LOOP, "amsMainAssignRun")
assert "amsInfoOffered" not in body and "amsMainCanAssign()" in body, \
    f"src/{LOOP}: amsMainAssignRun() (the button without a load cell) was changed"
assert squeeze("if (ams_main_assign_pending) { ams_main_assign_pending = false; amsMainAssignRun(); }") in \
    squeeze(code_of(LOOP)), f"src/{LOOP}: the pass of ams_main_assign_pending was changed"

# 3. The bay picker. The remembered order says whether a tap opened it; every
# note taken for a removal says no, and only amsPickOpenFor() says yes. The
# order is read before it is dropped, and the location question after a picker
# that closed without an answer is skipped for a tap.
PICK = "services/ams_pick.cpp"
pick_code = code_of(PICK)
pending = re.search(r"struct\s+Pending\s*\{([^{}]*)\}", pick_code)
assert pending and re.search(r"\bbool\s+by_tap\s*;", pending.group(1)), \
    f"src/{PICK}: struct Pending has no 'bool by_tap'"

where = f"src/{PICK}: amsPickNote()"
body = body_of(PICK, "amsPickNote")
assert "s_pending.by_tap=false;" in body, f"{where}: a note taken for a removal does not say by_tap = false"
assert "s_pending.by_tap=true" not in body, f"{where}: a note taken for a removal says by_tap = true"

where = f"src/{PICK}: amsPickOpenFor()"
body = body_of(PICK, "amsPickOpenFor")
assert body.count("s_pending.by_tap=true;") == 1, f"{where}: does not say by_tap = true exactly once"
before(body, "amsPickNote(spool_id,spool_name);", "s_pending.by_tap=true;", where)
before(body, "s_pending.by_tap=true;", "openPicker();", where)

where = f"src/{PICK}: onPicked()"
body = body_of(PICK, "onPicked")
unanswered, _ = block_after(body, "if(ams_id<0||tray_id<0){", where)
assert "s_pending.by_tap" in unanswered, f"{where}: the unanswered branch does not read by_tap"
before(unanswered, "s_pending.by_tap", "amsPickDropPending();", f"{where} (unanswered)")
before(unanswered, "amsPickDropPending();", "offerLocationAfterPick(spool_id,by_tap);", f"{where} (unanswered)")
assert body.count("offerLocationAfterPick(") == 1, f"{where}: offerLocationAfterPick() is called from more than one place"

where = f"src/{PICK}: offerLocationAfterPick()"
assert re.search(r"void\s+offerLocationAfterPick\s*\(\s*int\s+spool_id\s*,\s*bool\s+by_tap\s*\)", pick_code), \
    f"{where}: the signature is not (int spool_id, bool by_tap)"
body = body_of(PICK, "offerLocationAfterPick")
tapped, _ = block_after(body, "if(by_tap){", where)
assert "return;" in tapped and "logSD" in tapped, f"{where}: a picker opened by a tap does not return with a log line"
before(body, "if(by_tap){", "requestLocationPicker(true)", where)

# 4. The caption is the existing row, in all three languages (no U+2014).
lang_h = (src / "lang.h").read_text(encoding="utf-8")
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
sid, texts = "STR_AMS_MAIN_ASSIGN", ("Ins AMS", "Into AMS", "Dans l'AMS")
assert re.search(rf"\b{sid}\b\s*,", lang_h), f"src/lang.h: {sid} is not in the enum"
row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*' + sid + r"\b", lang_cpp)
assert row, f"src/lang.cpp: the row of {sid} is not there"
for lang_name, want, got in zip(("German", "English", "French"), texts, row.groups()):
    assert got == want, f"src/lang.cpp: {sid} ({lang_name}) reads '{got}', expected '{want}'"
    assert chr(0x2014) not in got, f"src/lang.cpp: {sid} ({lang_name}) has an em-dash"

# 5. The step is in the pipeline, right after the one for the finish (plan
# ruling R2: rules, entry, weights, finish, ams info).
steps = re.findall(r"- name: (Test [^\n]*)\n\s+run: ([^\n]*)", (root / ".github/workflows/ci.yml").read_text(encoding="utf-8"))
names = [name for name, _ in steps]
assert "Test ams info" in names, ".github/workflows/ci.yml: the step 'Test ams info' is not there"
assert names.index("Test ams info") == names.index("Test no tag finish") + 1, \
    ".github/workflows/ci.yml: 'Test ams info' does not come right after 'Test no tag finish'"
assert dict(steps)["Test ams info"] == "python3 test/test_ams_info.py", \
    ".github/workflows/ci.yml: the step does not run python3 test/test_ams_info.py"

print("ams info: ok")
