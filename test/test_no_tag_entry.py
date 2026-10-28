"""Read the source tree without a compiler and check the way into "create spool" without a tag (X, V35): the empty main screen offers "Spule anlegen" in place of "Heute getrocknet" and every other state keeps "Neu / Kopie", the choice sets the flag from the tag and the id on the pad before it computes its tiles, the database and the sort entry follow the flag, and every other way into creating (the unknown-tag card's actions, the card from a Bambu tag) clears it. The tap on the button drops what a lifted tag left on the screen before it opens the choice, through the one function the 60 s timer uses too."""
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


# 1. The right-hand button of the main screen. Nothing on the screen: "create
# spool" takes the place of "dried" (which has nothing to dry then), the label
# is the new text. Every state that shows the button otherwise keeps "New /
# Copy", and the label is set at the moment the button is shown, so that it
# cannot be left over from the state before.
MAIN = "ui/main_screen_helpers.cpp"
body = body_of(MAIN, "updateLinkButton")
empty, rest = block_after(body, "if(empty){", f"src/{MAIN}: updateLinkButton()")
assert squeeze("lv_obj_clear_flag(btn_copy, LV_OBJ_FLAG_HIDDEN)") in empty, \
    f"src/{MAIN}: updateLinkButton() does not show btn_copy on the empty screen"
assert squeeze("lv_obj_add_flag(btn_dried, LV_OBJ_FLAG_HIDDEN)") in empty, \
    f"src/{MAIN}: updateLinkButton() does not hide btn_dried on the empty screen"
assert squeeze("lv_obj_clear_flag(btn_dried, LV_OBJ_FLAG_HIDDEN)") not in empty, \
    f"src/{MAIN}: updateLinkButton() still shows btn_dried on the empty screen"
assert squeeze("setCopyButtonLabel(STR_BTN_CREATE_SPOOL)") in empty, \
    f"src/{MAIN}: updateLinkButton() does not label btn_copy STR_BTN_CREATE_SPOOL on the empty screen"
assert "STR_BTN_CREATE_SPOOL" not in rest, \
    f"src/{MAIN}: updateLinkButton() says STR_BTN_CREATE_SPOOL outside the empty screen"
# The two other branches that show the button: an archived spool and an
# unknown tag. Each shows it and names it, in that order.
shown = squeeze("lv_obj_clear_flag(btn_copy, LV_OBJ_FLAG_HIDDEN);setCopyButtonLabel(STR_BTN_COPY_SPOOL);")
assert rest.count(shown) == 2, \
    f"src/{MAIN}: updateLinkButton() does not label btn_copy STR_BTN_COPY_SPOOL right after showing it in both other branches (found {rest.count(shown)}, expected 2)"
assert rest.count(squeeze("lv_obj_clear_flag(btn_copy, LV_OBJ_FLAG_HIDDEN)")) == 2, \
    f"src/{MAIN}: updateLinkButton() shows btn_copy in a branch that does not label it"
# The label is the first child of the button, written through copyT() like the
# one that buildUI() makes, and only when the text changes.
body = body_of(MAIN, "setCopyButtonLabel")
for needle in ("lv_obj_get_child(btn_copy,0)", "copyT(", "lv_label_set_text("):
    assert needle in body, f"src/{MAIN}: setCopyButtonLabel() does not use {needle}"

# 2. The choice sets the flag from the tag and the id on the pad before it
# computes anything that reads it (the tiles), and names itself after the flag.
COPY = "ui/copy_flow.cpp"
body = body_of(COPY, "showCopyEntryPopup")
assert squeeze("noTagModeSet(noTagModeAtEntry(") in body, \
    f"src/{COPY}: showCopyEntryPopup() does not call noTagModeSet(noTagModeAtEntry(...))"
for needle in ("strlen(g_tag.tray_uuid)==32", "link_tag_uid[0]"):
    assert needle in body, f"src/{COPY}: showCopyEntryPopup() does not feed the flag from {needle}"
before(body, "noTagModeSet(noTagModeAtEntry(", "tagCreateOffered()", f"src/{COPY}: showCopyEntryPopup()")
before(body, "noTagModeSet(noTagModeAtEntry(", "dbPickOffered()", f"src/{COPY}: showCopyEntryPopup()")
before(body, "noTagModeSet(noTagModeAtEntry(", "copyEntryTiles(", f"src/{COPY}: showCopyEntryPopup()")
# Title and context line change with the flag; the helper that picks them is
# in the new unit, so the text ids are named there.
assert "noTagEntryTitle()" in body, f"src/{COPY}: showCopyEntryPopup() does not take its title from noTagEntryTitle()"
assert "noTagMode()" in body, f"src/{COPY}: showCopyEntryPopup() does not ask noTagMode() for the context line"
assert "STR_NOTAG_CTX" in body, f"src/{COPY}: showCopyEntryPopup() does not show STR_NOTAG_CTX"
body = body_of("ui/no_tag_create.cpp", "noTagEntryTitle")
assert "STR_BTN_CREATE_SPOOL" in body and "STR_BTN_COPY_SPOOL" in body, \
    "src/ui/no_tag_create.cpp: noTagEntryTitle() does not choose between STR_BTN_CREATE_SPOOL and STR_BTN_COPY_SPOOL"

# 3. The database and the sort entry follow the flag through the rule of
# services/no_tag_create_rules.h; what else they need stays as it was.
body = body_of("ui/db_pick_screen.cpp", "dbPickOffered")
assert squeeze("noTagSourceOffered(noTagMode()") in body, \
    "src/ui/db_pick_screen.cpp: dbPickOffered() does not call noTagSourceOffered(noTagMode(), ...)"
assert "fdbOffered()" in body, "src/ui/db_pick_screen.cpp: dbPickOffered() lost fdbOffered()"
body = body_of("ui/sort_entry_screen.cpp", "sortEntryOffered")
assert squeeze("noTagSourceOffered(noTagMode()") in body, \
    "src/ui/sort_entry_screen.cpp: sortEntryOffered() does not call noTagSourceOffered(noTagMode(), ...)"
assert "!backendIsFilaMan()" in body, "src/ui/sort_entry_screen.cpp: sortEntryOffered() lost !backendIsFilaMan()"

# 4. Every other way into creating clears the flag: the unknown-tag card's
# actions (before they branch), and the card built from a Bambu tag. NOT the
# card built for a database or sort entry (showTagCreatePopupFor()): those two
# enter it with the flag on and need it there.
POPUP = "ui/unknown_spool_popup.cpp"
body = body_of(POPUP, "runAction")
assert squeeze("noTagModeSet(false)") in body, f"src/{POPUP}: runAction() does not call noTagModeSet(false)"
before(body, "noTagModeSet(false)", "switch(", f"src/{POPUP}: runAction()")
CARD = "ui/tag_create_popup.cpp"
body = body_of(CARD, "showTagCreatePopup")
assert squeeze("noTagModeSet(false)") in body, f"src/{CARD}: showTagCreatePopup() does not call noTagModeSet(false)"
before(body, "noTagModeSet(false)", "tagCreateInputFromTag(", f"src/{CARD}: showTagCreatePopup()")
body = body_of(CARD, "showTagCreatePopupFor")
assert "noTagModeSet" not in body, \
    f"src/{CARD}: showTagCreatePopupFor() touches the flag, the database and the sort entry arrive here with it on"

# 5. The texts are in the table in all three languages, in the words the plan
# gives (no U+2014: a hyphen).
lang_h = (src / "lang.h").read_text(encoding="utf-8")
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
for sid, texts in (
    ("STR_BTN_CREATE_SPOOL", ("Spule anlegen", "Create spool", "Créer bobine")),
    ("STR_NOTAG_CTX", ("Ohne Tag - wird nicht gewogen", "No tag - not weighed", "Sans tag - non pesée")),
):
    assert re.search(rf"\b{sid}\b\s*,", lang_h), f"src/lang.h: {sid} is not in the enum"
    row = re.search(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}\s*,\s*//\s*' + sid + r"\b", lang_cpp)
    assert row, f"src/lang.cpp: the row of {sid} is not there"
    assert row.groups() == texts, f"src/lang.cpp: {sid} reads {row.groups()}, expected {texts}"

# 6. A tag that was lifted leaves its id behind for up to 60 s (the display is
# cleared by the timer, app_loop.cpp), while the button already reads "create
# spool" (nothing on the pad, nothing found). The choice would then open with
# the flag off, the lifted tag's id on its context line and a create that links
# to a tag no longer there. So the tap drops those leftovers first, through the
# same function the timer calls (which also ends the timer's wait, or it would
# clear the screen a second time in the middle of the create), and then opens
# the choice; in every other state it only opens the choice.
MAIN_SCREEN = "ui/main_screen.cpp"
code = squeeze(code_of(MAIN_SCREEN))
marker = squeeze("lv_obj_add_event_cb(btn_copy, [](lv_event_t *e) {")
assert code.count(marker) == 1, f"src/{MAIN_SCREEN}: btn_copy has {code.count(marker)} click handlers, expected 1"
handler, _ = block_after(code, marker, f"src/{MAIN_SCREEN}: btn_copy handler")
assert "noTagEntryTap()" in handler, f"src/{MAIN_SCREEN}: the btn_copy handler does not call noTagEntryTap()"
assert "showCopyEntryPopup()" not in handler, \
    f"src/{MAIN_SCREEN}: the btn_copy handler opens the choice itself, it would skip the leftovers of a lifted tag"
assert "logSD(" in handler, f"src/{MAIN_SCREEN}: the btn_copy handler lost its log line"

TAP = "ui/no_tag_create.cpp"
body = body_of(TAP, "noTagEntryTap")
guard = re.search(r"if\(([^{}]*)\)\{", body)
assert guard, f"src/{TAP}: noTagEntryTap() has no guarded block"
for needle in ("!tag_present", "!sm_found", "g_tag.uid_str[0]", "g_tag.tray_uuid[0]", "link_tag_uid[0]"):
    assert needle in guard.group(1), f"src/{TAP}: noTagEntryTap() does not guard the drop on {needle}"
dropped, outside = block_after(body, guard.group(0), f"src/{TAP}: noTagEntryTap()")
assert "dropLiftedTagDisplay()" in dropped, f"src/{TAP}: noTagEntryTap() does not call dropLiftedTagDisplay() in its guarded block"
assert "dropLiftedTagDisplay" not in outside, f"src/{TAP}: noTagEntryTap() drops the display outside the empty-state guard"
assert body.find("showCopyEntryPopup()") > body.find(guard.group(0)) + len(guard.group(0)) + len(dropped), \
    f"src/{TAP}: noTagEntryTap() does not open the choice after the drop"
assert "showCopyEntryPopup()" in outside, f"src/{TAP}: noTagEntryTap() does not open the choice in every state"

# The one function: the timer and the tap clear the display the same way, and
# it also ends the timer's wait.
LOOP = "app/app_loop.cpp"
body = body_of(LOOP, "dropLiftedTagDisplay")
for needle in ("clearTagDisplay()", "last_tag_seen_ms=0"):
    assert needle in body, f"src/{LOOP}: dropLiftedTagDisplay() does not do {needle}"
loop_body = body_of(LOOP, "appLoop")
assert "dropLiftedTagDisplay()" in loop_body, f"src/{LOOP}: the no-tag timer does not call dropLiftedTagDisplay()"
assert "clearTagDisplay()" not in loop_body, \
    f"src/{LOOP}: appLoop() still clears the display on its own, the timer and the tap have two ways to do it"
header = (src / "app/app_loop.h").read_text(encoding="utf-8")
assert "dropLiftedTagDisplay" in header, "src/app/app_loop.h: dropLiftedTagDisplay() is not declared"
nt_header = (src / "ui/no_tag_create.h").read_text(encoding="utf-8")
assert "noTagEntryTap" in nt_header, "src/ui/no_tag_create.h: noTagEntryTap() is not declared"

# 7. The id keypad ("Kopie per ID" from the choice) has no UID to show in the
# flag: its context line says "no tag" as the choice does, in the room of the
# longest of the three texts (bmax 55 in tools/lang_fr.jsonl, so 56 bytes).
# A Bambu tag still beats the flag (ruling R1), so the Bambu branch comes first.
KEYPAD = "ui/spool_flow.cpp"
body = body_of(KEYPAD, "showIdInputPopup")
ctx_start = body.find("lbl_ctx=lv_label_create(scr_link_id);")
ctx_end = body.find("lv_label_set_text(lbl_ctx,ctx_buf);")
assert 0 <= ctx_start < ctx_end, f"src/{KEYPAD}: showIdInputPopup() has no context line block"
ctx = body[ctx_start:ctx_end]
size = re.search(r"charctx_buf\[(\d+)\]", ctx)
assert size and int(size.group(1)) >= 56, \
    f"src/{KEYPAD}: showIdInputPopup() ctx_buf is too small for STR_NOTAG_CTX (needs 56, has {size and size.group(1)})"
for first, second in (("if(is_bambu)", "noTagMode()"), ("noTagMode()", "STR_NOTAG_CTX"), ("STR_NOTAG_CTX", "link_tag_uid")):
    before(ctx, first, second, f"src/{KEYPAD}: showIdInputPopup() context line")

print("no tag entry: ok")
