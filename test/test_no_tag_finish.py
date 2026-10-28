"""Read the source tree without a compiler and check how a spool created without a tag ends (X, V35): the copy's create and the card's create both end in noTagCreateFinish() while the flag is on, and in finishCopyFlow() with the first weighing's type question switched off only outside it; the finish closes the choice and the lists, shows the new spool by its id, says it in green, clears the flag, and arms no question for a label itself (that one follows the location picker, V39, see test_no_tag_location.py); the question is built and answered from the loop, and "Print" does what "More info" -> "Print label" does."""
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

POPUP = "ui/tag_create_popup.cpp"
CONFIRM = "ui/copy_confirm_popup.cpp"
FLOW = "ui/copy_flow.cpp"
CORE = "ui/no_tag_create.cpp"
HEADER = "ui/no_tag_create.h"
SPOOL_FLOW = "ui/spool_flow.cpp"
INTERNAL = "ui/spool_flow_internal.h"
COMMON = "ui/ui_common.cpp"
NAVIGATION = "ui/navigation.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def source_of(rel):
    """One file under src/ as the ratchet reads it."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    code = source_of(rel).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def block_of(body, head, where):
    """The code between the braces that follow head (it ends in an opening
    brace and occurs once), and the offsets in body where head starts and the
    block ends."""
    head = squeeze(head)
    assert head.endswith("{"), f"{where}: '{head}' must end in an opening brace"
    assert body.count(head) == 1, f"{where}: '{head}' is not there exactly once"
    at = body.find(head)
    open_at = at + len(head) - 1
    depth = 0
    for i in range(open_at, len(body)):
        if body[i] == "{":
            depth += 1
        elif body[i] == "}":
            depth -= 1
            if depth == 0:
                return body[open_at + 1:i], at, i + 1
    raise AssertionError(f"{where}: '{head}' has no closing brace")


def depth_at(body, offset):
    """How many braces are open at offset in body: 0 is the function's own level."""
    return body[:offset].count("{") - body[:offset].count("}")


def in_order(body, needles, where):
    """Every needle is in body, each one after the one before it."""
    at = -1
    for needle in needles:
        found = body.find(squeeze(needle), at + 1)
        assert found >= 0, f"{where}: '{needle}' is not there, or not after the one before it"
        at = found


def once(body, needle, where):
    """The needle is in body exactly once."""
    assert body.count(squeeze(needle)) == 1, f"{where}: '{needle}' is not there exactly once"


def yellow_after_finish(inner, finish_call, allowed, where):
    """After the finish call nothing in the flag's branch but the one guarded
    yellow write (allowed, exactly) and the return writes to the status line:
    a second write, or the one without its guard, could follow the Bambu chip
    hint and replace it."""
    finish = squeeze(finish_call)
    assert inner.count(finish) == 1, f"{where}: '{finish_call}' is not there exactly once"
    rest = inner[inner.find(finish) + len(finish):]
    once(rest, allowed, where + " (the yellow line, behind noTagYellowLineAllowed())")
    rest = rest.replace(squeeze(allowed), "", 1)
    assert rest == "return;", \
        f"{where}: after the finish only the guarded yellow line and the return may follow, found '{rest}'"


def handler_bodies(rel):
    """The squeezed bodies of every event handler in a file, log calls taken out."""
    source = source_of(rel)
    out = []
    for _, _, b0, b1 in cc.handlers(source):
        out.append(re.sub(r'logSDf?\([^;]*\);', "", squeeze(source.code[b0 + 1:b1])))
    return out


# 1. Both ways of creating end in noTagCreateFinish() while the flag is on:
# the copy's create (doCopySpoolCreate) and the card's create (runCreate).
# The branch comes right after the cache is forgotten and before anything
# the old ending does, so the code outside the flag stays as it was: the
# first weighing's type question is switched off (tareEntryFirstWeighDone)
# and the new spool is linked (finishCopyFlow) only there.
where = f"src/{FLOW}: doCopySpoolCreate()"
body = body_of(FLOW, "doCopySpoolCreate")
inner, start, end = block_of(body, "if (noTagMode()) {", where)
once(body, "noTagCreateFinish(", where)
once(inner, "noTagCreateFinish(new_id, copyCardVendor());", where + " in the flag")
assert inner.startswith(squeeze("noTagCreateFinish(new_id, copyCardVendor());")), \
    f"{where}: the flag's branch must finish first, the parts that did not come across follow"
# The yellow line for the parts left behind is the only thing that may write to
# the status line after the finish, and only where noTagYellowLineAllowed()
# lets it (the Bambu chip hint is the last word, spec 5.3 X); the code view
# blanks the text of a string, so it reads "".
yellow_after_finish(inner, "noTagCreateFinish(new_id, copyCardVendor());",
                    'if (missing != CM_NONE && noTagYellowLineAllowed(new_id, "", missing)) copyCreatedStatus(missing);',
                    where + " in the flag")
assert inner.endswith("return;"), f"{where}: the flag's branch must leave, nothing of the old ending may run after it"
for banned in ("finishCopyFlow(", "tareEntryFirstWeighDone(", "doLinkPatch"):
    assert banned not in inner, f"{where}: {banned} runs in the flag"
before, after = body[:start], body[end:]
assert "(code==200||code==201)&&new_id>0" in before and "spoolCacheForget(" in before, \
    f"{where}: the flag's branch is not on the way out of a created spool, after the cache was forgotten"
assert "noTagCreateFinish(" not in after and "noTagMode()" not in after, \
    f"{where}: the flag is decided more than once"
once(after, "if (copy_create_asked) tareEntryFirstWeighDone(new_id);", where + " outside the flag")
once(after, "if (finishCopyFlow(new_id)) copyCreatedStatus(missing);", where + " outside the flag")
assert after.find("tareEntryFirstWeighDone(") < after.find("finishCopyFlow("), \
    f"{where}: the old ending changed its order"
once(body, "tareEntryFirstWeighDone(", where)
once(body, "finishCopyFlow(", where)

where = f"src/{POPUP}: runCreate()"
body = body_of(POPUP, "runCreate")
inner, start, end = block_of(body, "if (noTagMode()) {", where)
once(body, "noTagCreateFinish(", where)
in_order(inner, ["closeTagCreatePopup();", "noTagCreateFinish(r.spool_id, s_in.vendor);", "return;"],
         where + " in the flag")
yellow_after_finish(inner, "noTagCreateFinish(r.spool_id, s_in.vendor);",
                    'if (r.price_lost && noTagYellowLineAllowed(r.spool_id, "", 0)) '
                    'statusMessageShow(T(STR_TAGNEW_PRICE_LOST), UI_COL_WARN);',
                    where + " in the flag")
assert inner.endswith("return;"), f"{where}: the flag's branch must leave, nothing of the old ending may run after it"
for banned in ("finishCopyFlow(", "tareEntryFirstWeighDone(", "doLinkPatch"):
    assert banned not in inner, f"{where}: {banned} runs in the flag"
before, after = body[:start], body[end:]
assert "(code==200||code==201)&&r.spool_id>0" in before and "spoolCacheForget(" in before, \
    f"{where}: the flag's branch is not on the way out of a created spool, after the cache was forgotten"
assert "tareEntryFirstWeighDone(" not in before, \
    f"{where}: the first weighing's type question is switched off before the flag is asked"
assert "noTagCreateFinish(" not in after and "noTagMode()" not in after, \
    f"{where}: the flag is decided more than once"
once(after, "if (s_in.spool_type_asked) tareEntryFirstWeighDone(r.spool_id);", where + " outside the flag")
once(after, "if (!finishCopyFlow(r.spool_id, link_id)) return;", where + " outside the flag")
in_order(after, ["tareEntryFirstWeighDone(", "closeTagCreatePopup();", "finishCopyFlow("], where + " outside the flag")
once(body, "tareEntryFirstWeighDone(", where)
once(body, "finishCopyFlow(", where)

# The vendor of a copy is read off the copy of the look the card keeps at file
# scope, which closing the card leaves alone (copyFlowDeferredActions() closes
# it before the create runs); without a look the vendor is not known.
body = body_of(CONFIRM, "copyCardVendor")
assert body == squeeze('return copy_template_has_look ? copy_template_look.vendor : "";'), \
    f"src/{CONFIRM}: copyCardVendor() must return the kept look's vendor, or an empty string without one"
assert squeeze("const char* copyCardVendor();") in squeeze(source_of(INTERNAL).code), \
    f"src/{INTERNAL}: copyCardVendor() is not declared"
body = body_of(CONFIRM, "closeCopyConfirmPopup")
assert "copy_template_look" not in body and "copy_template_has_look" not in body, \
    f"src/{CONFIRM}: closeCopyConfirmPopup() clears the look the vendor is read from"

# 2. The ending itself. In this order, none of it conditional on the others:
# the picker's screens and the choice go with the main screen (the way the
# link's ending leaves them), the new number is shown as after "spool by ID",
# the line says so in green, the flag goes off. There is no link and no mark
# for the first weighing in it, and no card: the question is built by the loop.
where = f"src/{CORE}: noTagCreateFinish()"
body = body_of(CORE, "noTagCreateFinish")
in_order(body, ["linkPickerClose();", "showMainScreen();", "idSpoolAdopt(new_spool_id);",
                "statusMessageShow(msg, UI_COL_GOOD);", "noTagModeSet(false);"], where)
for needle in ("linkPickerClose();", "showMainScreen();", "idSpoolAdopt(new_spool_id);",
               "statusMessageShow(msg, UI_COL_GOOD);", "noTagModeSet(false);"):
    once(body, needle, where)
    at = body.find(squeeze(needle))
    # Its own statement on the function's level: no brace around it, and no
    # "if (...)" or "else" in front of it that a brace-less body would hide.
    assert depth_at(body, at) == 0 and body[at - 1] in ";}{", f"{where}: '{needle}' is conditional"
once(body, "const bool bambu = noTagCreatedNote(vendor) == NTN_BAMBU_CHIP;", where)
once(body, "snprintf(msg, sizeof(msg), T(bambu ? STR_NOTAG_CREATED_BAMBU : STR_NOTAG_CREATED), new_spool_id);", where)
assert body.find("snprintf(msg") < body.find("statusMessageShow("), f"{where}: the line is said before it is written"
for banned in ("finishCopyFlow(", "doLinkPatch", "tareEntryFirstWeighDone(", "lv_", "buildAsk("):
    assert banned not in body, f"{where}: {banned} must not be in the ending (no link, no mark, no card built here)"
# The ending arms no question for a label (V39): it arms the location picker,
# and the question follows once the picker has ended, for a spool still on the
# screen with a printer ready. Where and when, test_no_tag_location.py.
assert "s_ask_for" not in body and "s_ask_wait" not in body, \
    f"{where}: the ending arms the question for a label itself; it follows the location picker"

# The Bambu chip hint is the last word on the status line (spec 5.3 X: the one
# line that keeps Bambuddy from making the roll a second time at the first
# AMS). The finish remembers whether it wrote the hint, and a caller asks
# noTagYellowLineAllowed() before it writes a yellow line over the green one:
# plain note, yes (as it always was); chip hint, no, the loss goes to the log.
once(body, "s_chip_hint = bambu;", where)
assert depth_at(body, body.find("s_chip_hint=")) == 0 and body[body.find("s_chip_hint=") - 1] in ";}{", \
    f"{where}: the hint is remembered conditionally"
assert body.find("statusMessageShow(msg, UI_COL_GOOD);") < body.find("s_chip_hint="), \
    f"{where}: the hint is remembered before it is written"
where = f"src/{CORE}: noTagYellowLineAllowed()"
body = body_of(CORE, "noTagYellowLineAllowed")
assert body.startswith(squeeze("if (!s_chip_hint) return true;")) and body.endswith("returnfalse;"), \
    f"{where}: must allow the yellow line when no chip hint stands, and refuse it otherwise"
assert body.count("returntrue;") == 1 and body.count("returnfalse;") == 1, f"{where}: more ways out than the two"
assert "logSDf(" in body[body.find("returntrue;"):] and "statusMessageShow(" not in body and "lbl_status" not in body, \
    f"{where}: a refused yellow line goes to the log, not to the status line"
assert squeeze("bool noTagYellowLineAllowed(int new_spool_id, const char* lost, unsigned parts);") \
    in squeeze(source_of(HEADER).code), f"src/{HEADER}: noTagYellowLineAllowed() is not declared"

# 3. The question. It is built from the loop, a pass after it was armed (the
# screens closed in that pass go back to the pool only after LVGL ran), it
# stays up only while its spool is the one on the screen, its buttons only
# park the answer, and "Print" does what "More info"
# -> "Print label" does (the print sheet unless the printer screen switched it
# off, else the label straight away), once the loop picks the answer up.
where = f"src/{CORE}: shownStill()"
body = body_of(CORE, "shownStill")
assert body == squeeze("return spool_id > 0 && !tag_present && sm_found && sm_id == spool_id;"), \
    f"{where}: must be true only for a spool shown by its number with no tag on the pad"

where = f"src/{CORE}: noTagLabelAskTick()"
body = body_of(CORE, "noTagLabelAskTick")
inner, start, end = block_of(body, "if (s_card) {", where)
once(inner, "const bool print = answer == LA_PRINT && shownStill(s_card_for);", where + " answered")
once(inner, "if (answer == LA_NONE && shownStill(s_card_for)) return;", where + " waiting")
in_order(inner, ["if (answer == LA_NONE && shownStill(s_card_for)) return;", "const bool print =",
                 "closeNoTagLabelAsk();", "if (print) {"], where + " answered")
printing, _, _ = block_of(inner, "if (print) {", where + " answered")
assert printing == squeeze("if (labelPrinterAskFirst()) show_print_sheet_pending = true; "
                           "else print_spool_label_pending = true;"), \
    f"{where}: 'Print' must set show_print_sheet_pending when labelPrinterAskFirst(), else print_spool_label_pending"
assert body.count("show_print_sheet_pending") == 1 and body.count("print_spool_label_pending") == 1, \
    f"{where}: the label is asked for in more than one place"
rest = body[end:]
in_order(rest, ["if (!s_ask_for) return;", "if (s_ask_wait) { s_ask_wait = false; return; }",
                "const int id = s_ask_for;", "s_ask_for = 0;",
                "if (shownStill(id) && labelPrinterReadiness() == LP_OK) buildAsk(id);"], where + " building")

# The buttons park their answer and do nothing else; nothing the print needs
# is touched from a tap (the NVS read of the setting, the flags of the loop).
handlers = handler_bodies(CORE)
assert sorted(handlers) == sorted(["s_answer=LA_PRINT;", "s_answer=LA_NO;"]), \
    f"src/{CORE}: the buttons must park their answer and do nothing else, found {handlers}"
body = body_of(CORE, "buildAsk")
assert "labelPrinterAskFirst();" in body, f"src/{CORE}: buildAsk() does not read the setting once, so the loop's print finds it cached"
assert squeeze("askButton(box, UI_CARD_ROW_X, true, STR_PS_PRINT)") in body and \
    squeeze("askButton(box, UI_POPUP_W - NTA_BTN_W - UI_CARD_ROW_X, false, STR_AMS_TIMER_NO)") in body, \
    f"src/{CORE}: the buttons are 'Print' (STR_PS_PRINT) on the left and 'No' (STR_AMS_TIMER_NO) on the right"
assert "STR_LABEL_ASK" in body, f"src/{CORE}: buildAsk() does not use the question's title"
assert "lv_scr_act()" in body, f"src/{CORE}: the question stands on the active screen like the other cards"

# Anything that clears the screens (a tap elsewhere, a web action) takes the
# question with it, and while it is up the loop holds off its own questions.
body = body_of(CORE, "closeNoTagLabelAsk")
for needle in ("releaseScreen(&s_card);", "s_ask_for = 0;", "s_answer = LA_NONE;"):
    assert squeeze(needle) in body, f"src/{CORE}: closeNoTagLabelAsk() does not do {needle}"
assert "closeNoTagLabelAsk();" in body_of(NAVIGATION, "hideAllOverlays"), \
    f"src/{NAVIGATION}: hideAllOverlays() does not close the label question"
assert "noTagLabelAskOpen()" in body_of(COMMON, "uiModalWaiting"), \
    f"src/{COMMON}: uiModalWaiting() does not count the label question, the first weighing would ask over it"
assert body_of(CORE, "noTagLabelAskOpen") == squeeze("return s_card != nullptr;"), \
    f"src/{CORE}: noTagLabelAskOpen() must say whether the card is up"

# The loop runs it where it runs the card's own tick.
source = source_of(SPOOL_FLOW)
spans = [(s, e) for _, s, e in cc.find_functions(source.code)
         if "tagCreatePopupTick();" in squeeze(source.code[s + 1:e])]
assert len(spans) == 1, f"src/{SPOOL_FLOW}: tagCreatePopupTick() is not called in exactly one function"
loop_body = squeeze(source.code[spans[0][0] + 1:spans[0][1]])
assert loop_body.count("noTagLabelAskTick();") == 1 and \
    abs(loop_body.find("noTagLabelAskTick();") - loop_body.find("tagCreatePopupTick();")) < 40, \
    f"src/{SPOOL_FLOW}: noTagLabelAskTick() does not run next to tagCreatePopupTick()"

header = squeeze(source_of(HEADER).code)
for decl in ("void noTagCreateFinish(int new_spool_id, const char* vendor);", "void noTagLabelAskTick();",
             "bool noTagLabelAskOpen();", "void closeNoTagLabelAsk();"):
    assert squeeze(decl) in header, f"src/{HEADER}: {decl} is not declared"

# 4. The texts are in the table in all three languages (no U+2014: a hyphen).
# The two answers are the existing rows, which read exactly "Print" and "No".
lang_h = (src / "lang.h").read_text(encoding="utf-8")
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")

for sid, texts in (
    ("STR_NOTAG_CREATED", ("Spule %d angelegt - ohne Tag", "Spool %d created - no tag", "Bobine %d créée - sans tag")),
    ("STR_NOTAG_CREATED_BAMBU", ("Spule %d - Chip vor AMS auflegen", "Spool %d - read chip before AMS",
                                 "Bobine %d - lire la puce avant l'AMS")),
    ("STR_LABEL_ASK", ("Etikett drucken?", "Print label?", "Imprimer l'étiquette ?")),
    ("STR_PS_PRINT", ("Drucken", "Print", "Imprimer")),
    ("STR_AMS_TIMER_NO", ("Nein", "No", "Non")),
):
    assert re.search(rf"\b{sid}\b\s*,", lang_h), f"src/lang.h: {sid} is not in the enum"
    row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*' + sid + r"\b", lang_cpp)
    assert row, f"src/lang.cpp: the row of {sid} is not there"
    for lang_name, want, got in zip(("German", "English", "French"), texts, row.groups()):
        assert got == want, f"src/lang.cpp: {sid} ({lang_name}) reads '{got}', expected '{want}'"
        assert chr(0x2014) not in got, f"src/lang.cpp: {sid} ({lang_name}) has an em-dash"
        if sid.startswith("STR_NOTAG_CREATED"):
            assert got.count("%d") == 1 and "%" not in got.replace("%d", ""), \
                f"src/lang.cpp: {sid} ({lang_name}) must have the number and nothing else to fill in"

# 5. The step is in the pipeline, right after the one for the weights (plan
# ruling R2: rules, entry, weights, finish, ams info).
steps = re.findall(r"- name: (Test [^\n]*)\n\s+run: ([^\n]*)", (root / ".github/workflows/ci.yml").read_text(encoding="utf-8"))
names = [name for name, _ in steps]
assert "Test no tag finish" in names, ".github/workflows/ci.yml: the step 'Test no tag finish' is not there"
assert names.index("Test no tag finish") == names.index("Test no tag weights") + 1, \
    ".github/workflows/ci.yml: 'Test no tag finish' does not come right after 'Test no tag weights'"
assert dict(steps)["Test no tag finish"] == "python3 test/test_no_tag_finish.py", \
    ".github/workflows/ci.yml: the step does not run python3 test/test_no_tag_finish.py"

# U2 (Festlegung 9): a spool shown by its id asks after a lift what a lifted
# tag asks (original b7bc11a). A spool just created without a tag is such a
# spool, and its own questions come first: the location picker (V39), then
# "Print a label?". The questions after a lift wait for both, through one
# predicate that covers the whole way from the create's end to the last answer:
# the picker armed by the ending (s_loc_for, its pass of delay included), the
# picker asked for or up with the label question waiting on its end (the
# waiter in more_info_screen.cpp), the label question armed (s_ask_for, its
# pass of delay included), and the card up.
where = f"src/{CORE}: noTagAfterCreateBusy()"
busy = body_of(CORE, "noTagAfterCreateBusy")
assert busy.startswith("return") and busy.count(";") == 1, \
    f"{where}: must be one return of the states it covers, found '{busy}'"
for needle, what in (("s_loc_for!=0", "the picker armed by the ending"),
                     ("locationPickerThenPending(locationPickerEnded)", "the picker asked for or up"),
                     ("s_ask_for!=0", "the label question armed"),
                     ("noTagLabelAskOpen()", "the label card up")):
    assert needle in busy, f"{where}: does not cover {what} ('{needle}')"
assert "bool noTagAfterCreateBusy();" in (src / HEADER).read_text(encoding="utf-8"), \
    f"src/{HEADER}: noTagAfterCreateBusy() is not declared"
INFO, INFO_H = "ui/more_info_screen.cpp", "ui/more_info_screen.h"
assert body_of(INFO, "locationPickerThenPending") == squeeze("return ended && s_loc_ended.fn == ended;"), \
    f"src/{INFO}: locationPickerThenPending() must only say whether this waiter is still pending"
assert squeeze("bool locationPickerThenPending(void (*ended)(int spool_id));") in \
    squeeze((src / INFO_H).read_text(encoding="utf-8")), \
    f"src/{INFO_H}: locationPickerThenPending() is not declared"

LOOP = "app/app_loop.cpp"
loop = body_of(LOOP, "appLoop")
lift = re.search(r"if\(\(loc_popup_pending_id>0\|\|ams_popup_pending_id>0\|\|"
                 r"pick_popup_pending_id>0\)&&([^{]*)\)\{", loop)
assert lift, f"src/{LOOP}: the block that asks after a lift is not there"
assert "!noTagAfterCreateBusy()" in lift.group(1), \
    f"src/{LOOP}: the questions after a lift do not wait for the location picker and the label question"

# The lift clock of that spool (original: 15 s, not over while a screen covers
# the main one) does not run while the create's own questions are open: the
# picker and the card cover the screen, but the clock ran on behind them, and
# in an uncovered pass between them (before the card is built) or the pass the
# card closed in it ended the spool before the questions were asked, "Print"
# included. The clock restarts as long as the predicate holds, so the 15 s
# count from the last answer.
ID_SPOOL = "app/id_spool.cpp"
where = f"src/{ID_SPOOL}: idSpoolTick()"
body = body_of(ID_SPOOL, "idSpoolTick")
inner, _, _ = block_of(body, "else if (noTagAfterCreateBusy()) {", where)
assert inner == squeeze("s_lifted_ms = millis();"), \
    f"{where}: under the create's questions the lift clock must restart and nothing else, found '{inner}'"
in_order(body, ["if (!s_lifted_ms) {", "else if (noTagAfterCreateBusy()) {",
                "else if (millis() - s_lifted_ms > ID_SPOOL_LIFTED_MS && !uiMainScreenCovered()) {"], where)
assert '#include "ui/no_tag_create.h"' in (src / ID_SPOOL).read_text(encoding="utf-8"), \
    f"src/{ID_SPOOL}: noTagAfterCreateBusy() is used without its header"

print("no tag finish: ok")
