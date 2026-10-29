"""Read the source tree without a compiler and check the screens around several identical spools made in one go (n10, V44): a create without a tag (X) can make up to ten spools that are alike. The rules (the count, the step after each try, the ids as text) are services/batch_create_rules.h with their own compiled test, test_batch_create_rules.py; this one holds the screens to them. One function per part, in the order of the plan:

  part_card()   the count on both create cards, only in the flag, and the create button naming it
"""
import json
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

BATCH_H = "ui/batch_create.h"
BATCH = "ui/batch_create.cpp"
RULES = "services/batch_create_rules.h"
CONFIRM = "ui/copy_confirm_popup.cpp"
POPUP = "ui/tag_create_popup.cpp"
NOTAG = "ui/no_tag_create.cpp"


# ------------------------------------------------------------
#  Helpers
# ------------------------------------------------------------

def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def source_of(rel):
    """One file under src/ as the ratchet reads it."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def text_of(rel):
    """One file under src/ as it is written, comments and strings included."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return path.read_text(encoding="utf-8")


def code_of(rel):
    """One file under src/ without comments and string contents, blanks gone."""
    return squeeze(source_of(rel).code)


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    code = source_of(rel).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def handler_of(rel, marker):
    """The body of the one event handler (lambda or named) that contains marker, blanks gone."""
    source = source_of(rel)
    found = [squeeze(source.code[b0 + 1:b1]) for _, _, b0, b1 in cc.handlers(source)
             if squeeze(marker) in squeeze(source.code[b0 + 1:b1])]
    assert len(found) == 1, f"src/{rel}: {len(found)} event handlers contain {marker}, expected one"
    return found[0]


def in_order(body, needles, where):
    """Every needle is in body, each one after the one before it."""
    at = -1
    for needle in needles:
        found = body.find(squeeze(needle), at + 1)
        assert found >= 0, f"{where}: '{needle}' is not there, or not after the one before it"
        at = found


def braces_close(code, opening):
    """Offset of the brace that closes the one at code[opening]."""
    depth = 0
    for i in range(opening, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    raise AssertionError("unbalanced braces")


def callback_body(rel, body, call):
    """The body of what is handed to call as its last argument: a lambda
    written there, or a function of the file named there."""
    at = body.find(squeeze(call))
    assert at >= 0, f"src/{rel}: no {call}"
    arg = body[at + len(squeeze(call)):]
    if arg.startswith("["):
        opening = arg.index("{")
        return arg[opening + 1:braces_close(arg, opening)]
    name = re.match(r"[A-Za-z_]\w*", arg)
    assert name, f"src/{rel}: {call} is handed neither a lambda nor a function name"
    return body_of(rel, name.group(0))


def define_of(rel, name):
    """The whole-number value of the one #define name in a file under src/."""
    found = re.findall(rf"^#define[ \t]+{name}[ \t]+([0-9]+)\b", text_of(rel), re.M)
    assert len(found) == 1, f"src/{rel}: #define {name} is not defined exactly once"
    return int(found[0])


def unconditional_in(body, needle, where):
    """needle is in body, and nothing before it can leave the function or put
    it under a condition: no return before it, no block left open around it."""
    at = body.find(squeeze(needle))
    assert at >= 0, f"{where}: does not call {needle}"
    before = body[:at]
    assert "return" not in before, f"{where}: can return before {needle}"
    assert before.count("{") == before.count("}"), f"{where}: {needle} stands in a block of its own (a condition)"


# ------------------------------------------------------------
#  Part 1: the count on the cards (Task 7)
# ------------------------------------------------------------

def part_card():
    """Both create cards get the count at their top right, only in the flag;
    a tap opens the price pad; a valid answer is taken and the card shows it on
    its create button, an invalid one changes nothing and shows the range. The
    create button reads the count unless the card asks for something first
    ("Fill weight", "Density"). The flag puts the count back to one whenever
    it is set, and the texts are in the table, the French and the budgets."""

    # 1. The interface of the new unit, as the plan names it.
    header = code_of(BATCH_H)
    for decl in ("int batchCount();", "void batchCountReset();",
                 "void batchCountChip(lv_obj_t* box, void (*changed)());",
                 "const char* batchCreateCaption(char* buf, size_t cap);"):
        assert squeeze(decl) in header, f"src/{BATCH_H}: does not declare {decl}"

    # 2. The button: at the top right of the card (x = 400 - 8 - width, y 8,
    # 36 high), above the title and clear of the icon in the middle. The copy
    # card behind Bambuddy on Spoolman is 304 of the screen's 320 px high, so
    # the count cannot be a line of its own.
    assert define_of(BATCH, "BATCH_CHIP_EDGE") == 8, f"src/{BATCH}: BATCH_CHIP_EDGE is not 8"
    assert define_of(BATCH, "BATCH_CHIP_H") == 36, f"src/{BATCH}: BATCH_CHIP_H is not 36"
    chip_w = define_of(BATCH, "BATCH_CHIP_W")
    title_y = define_of("ui/theme.h", "UI_CARD_TITLE_Y")
    assert 8 + 36 <= title_y, f"src/{BATCH}: the button reaches into the title at y {title_y}"
    # The icon is centred and UI_FONT_ICON is 24 px: the button stays right of it.
    assert 400 - 8 - chip_w > 200 + 24, f"src/{BATCH}: a button {chip_w} px wide reaches the icon in the middle"
    chip = body_of(BATCH, "batchCountChip")
    for needle in ("lv_obj_set_size(btn,BATCH_CHIP_W,BATCH_CHIP_H)",
                   "lv_obj_set_pos(btn,UI_POPUP_W-BATCH_CHIP_EDGE-BATCH_CHIP_W,BATCH_CHIP_EDGE)",
                   "s_changed=changed"):
        assert needle in chip, f"src/{BATCH}: batchCountChip() does not do {needle}"

    # 3. The tap opens the price pad with the count filled in, under its own
    # title and grey line; the answer goes to a function of this file.
    pad = "showPricePadInfo(batchCount(),STR_BATCH_COUNT_TITLE,STR_BATCH_COUNT_INFO,"
    tap = handler_of(BATCH, pad)
    answer = re.search(re.escape(pad) + r"([A-Za-z_]\w*)\)", tap)
    assert answer, f"src/{BATCH}: the tap does not hand the pad's answer to a function"
    body = body_of(BATCH, answer.group(1))
    where = f"src/{BATCH}: {answer.group(1)}()"
    # Only a card that still stands, and only in the flag: a count typed for a
    # card that was closed meanwhile reaches no create.
    assert "noTagMode()" in body[:body.find("batchCountValid(")], \
        f"{where}: takes a count without asking noTagMode() first"
    # Invalid: the range on the button, nothing taken, the card not told.
    in_order(body, ["batchCountValid(", "STR_BATCH_COUNT_RANGE", "return;", "s_count=", "s_changed()"], where)
    refused = body[body.find("STR_BATCH_COUNT_RANGE"):]
    refused = refused[:refused.find("return;")]
    assert "s_count=" not in refused and "s_changed" not in refused, \
        f"{where}: an invalid count is taken, or the card is told about it"
    assert "s_count=BATCH_COUNT_MIN" in body_of(BATCH, "batchCountReset"), \
        f"src/{BATCH}: batchCountReset() does not go back to BATCH_COUNT_MIN"
    caption = body_of(BATCH, "batchCreateCaption")
    for needle in ("STR_BATCH_CREATE_N", "STR_COPY_CARD_CREATE", "s_count"):
        assert needle in caption, f"src/{BATCH}: batchCreateCaption() does not use {needle}"

    # 4. The copy card: the button only in the flag, and a new count builds
    # the card again. The create button reads the count, unless the fill
    # weight is missing: then it asks for it, as before.
    code = code_of(CONFIRM)
    assert code.count("batchCountChip(") == 1, f"src/{CONFIRM}: batchCountChip() is not called exactly once"
    body = body_of(CONFIRM, "buildCopyCard")
    assert "if(noTagMode())batchCountChip(box,buildCopyCard);" in body, \
        f"src/{CONFIRM}: buildCopyCard() does not build the count as if (noTagMode()) batchCountChip(box, buildCopyCard)"
    assert "fill_missing?T(STR_NOTAG_FILL_BTN):batchCreateCaption(" in body, \
        f"src/{CONFIRM}: the create button does not read batchCreateCaption() with the fill weight first"
    assert code.count("lv_obj_get_child(btn_ok,0)") == 1, \
        f"src/{CONFIRM}: the create button's text is written in more than one place"

    # 5. The card for a new spool: the button only in the flag. A new count
    # only puts the text on the create button again: after an answer that got
    # lost the button stays disabled (a second try would make the spool twice),
    # so the count must neither show the plan again nor free the button.
    code = code_of(POPUP)
    assert code.count("batchCountChip(") == 1, f"src/{POPUP}: batchCountChip() is not called exactly once"
    body = body_of(POPUP, "buildCard")
    assert "if(noTagMode())batchCountChip(box," in body, \
        f"src/{POPUP}: buildCard() does not build the count as if (noTagMode()) batchCountChip(box, ...)"
    changed = callback_body(POPUP, body, "batchCountChip(box,")
    assert "setOkText(s_ok_text)" in changed, \
        f"src/{POPUP}: a new count does not put the button's own text on again (setOkText(s_ok_text))"
    for banned in ("showPlan(", "showStatus(", "lv_obj_clear_state(", "s_job="):
        assert banned not in changed, f"src/{POPUP}: a new count does {banned}, it may only put the text on"
    # One place writes the left button's text, and it reads the count for
    # "Create"; every other text ("Fill weight", "Density") is its own.
    assert code.count("lv_obj_get_child(s_btn_ok,0)") == 1, \
        f"src/{POPUP}: the left button's text is written in more than one place"
    body = body_of(POPUP, "setOkText")
    for needle in ("s_ok_text=text_id", "text_id==STR_COPY_CARD_CREATE?batchCreateCaption(", ":T(text_id)",
                   "lv_obj_get_child(s_btn_ok,0)"):
        assert needle in body, f"src/{POPUP}: setOkText() does not do {needle}"
    assert body.find("s_ok_text=text_id") < body.find("if(!s_btn_ok)return;"), \
        f"src/{POPUP}: setOkText() forgets the text asked for when there is no button yet"
    # "Create" whenever the status changes; the asks put their own text on
    # after it, so the fill weight and the density come before the count.
    assert "setOkText(STR_COPY_CARD_CREATE)" in body_of(POPUP, "showStatus"), \
        f"src/{POPUP}: showStatus() does not put the create text back"
    in_order(body_of(POPUP, "showAsk"), ["showStatus(", "setOkText(button_id)"], f"src/{POPUP}: showAsk()")
    assert "showAsk(hint_id,STR_NOTAG_FILL_BTN)" in body_of(POPUP, "showFillAsk"), \
        f"src/{POPUP}: showFillAsk() does not name the left button STR_NOTAG_FILL_BTN"
    assert "showAsk(hint_id,STR_DENSITY_BTN)" in body_of(POPUP, "showDensityAsk"), \
        f"src/{POPUP}: showDensityAsk() does not name the left button STR_DENSITY_BTN"
    # The count is on the button from the first paint, not only once the plan
    # has answered.
    in_order(body_of(POPUP, "buildAnswers"), ["s_btn_ok=answerButton(", "setOkText(STR_COPY_CARD_CREATE)"],
             f"src/{POPUP}: buildAnswers()")

    # 6. The flag puts the count back to one whenever it is set, not only when
    # it changes: a cancelled create leaves the flag on, and the next "Create
    # spool" sets it on again, which is no change.
    body = body_of(NOTAG, "noTagModeSet")
    unconditional_in(body, "batchCountReset();", f"src/{NOTAG}: noTagModeSet()")
    for rel in (CONFIRM, POPUP, NOTAG):
        assert '#include "ui/batch_create.h"' in text_of(rel), f"src/{rel}: does not include ui/batch_create.h"

    # 7. The texts: in lang.h, lang.cpp, the French working file and the width
    # budgets. German in the plan's words; the range held to the rules.
    lo, hi = define_of(RULES, "BATCH_COUNT_MIN"), define_of(RULES, "BATCH_COUNT_MAX")
    assert (lo, hi) == (1, 10), f"src/{RULES}: the range is {lo} to {hi}, the texts are written for 1 to 10"
    lang_h = text_of("lang.h")
    lang_cpp = text_of("lang.cpp")
    work = {}
    for line in (root / "tools" / "lang_fr.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            entry = json.loads(line)
            work[entry["id"]] = entry
    budgets = [line.split("\t") for line in (root / "tools" / "ui_budgets.tsv").read_text(encoding="utf-8").splitlines()
               if line and not line.startswith("#")]
    ranges = (f"{lo} bis {hi}", f"{lo} to {hi}", f"{lo} à {hi}")
    for sid, german, conv, in_range in (
        ("STR_BATCH_COUNT_CHIP", "Anzahl %d", 1, False),
        ("STR_BATCH_COUNT_TITLE", None, 0, False),
        ("STR_BATCH_COUNT_INFO", None, 0, True),
        ("STR_BATCH_COUNT_RANGE", "1 bis 10", 0, True),
        ("STR_BATCH_CREATE_N", "%d anlegen", 1, False),
    ):
        assert re.search(rf"\b{sid}\b\s*,", lang_h), f"src/lang.h: {sid} is not in the enum"
        row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                        + sid + r"\b", lang_cpp)
        assert row, f"src/lang.cpp: the row of {sid} is not there"
        for lang_name, got, span in zip(("German", "English", "French"), row.groups(), ranges):
            assert got.strip(), f"src/lang.cpp: {sid} has no {lang_name} text"
            assert "\N{EM DASH}" not in got, f"src/lang.cpp: {sid} ({lang_name}) has an em-dash"
            assert got.count("%") == conv and got.count("%d") == conv, \
                f"src/lang.cpp: {sid} ({lang_name}) '{got}' does not have exactly {conv} %d"
            if in_range:
                assert span in got, f"src/lang.cpp: {sid} ({lang_name}) '{got}' does not name the range '{span}'"
        if german is not None:
            assert row.group(1) == german, f"src/lang.cpp: {sid} reads '{row.group(1)}' in German, expected '{german}'"
        entry = work.get(sid)
        assert entry, f"tools/lang_fr.jsonl: {sid} is missing"
        assert entry["st"] == "ok" and entry["fr"] == row.group(3), \
            f"tools/lang_fr.jsonl: {sid} is not translated, or not the French of lang.cpp"
        assert any(b[0] == sid for b in budgets), f"tools/ui_budgets.tsv: {sid} has no width budget"

    # The button's two texts fit it by the house button rule (4 px of air each
    # side), the create button's the narrowest answer beside the price
    # (TCP_ANSWER_W, COPY_CARD_ANSWER_W: 140 px, less 8).
    for sid, font, budget in (("STR_BATCH_COUNT_CHIP", "14", chip_w - 8), ("STR_BATCH_COUNT_RANGE", "14", chip_w - 8),
                              ("STR_BATCH_CREATE_N", "18", 132)):
        rows = [b for b in budgets if b[0] == sid]
        assert any(b[1] == font and int(b[2]) <= budget and b[3] == "NEVER" for b in rows), \
            f"tools/ui_budgets.tsv: {sid} has no NEVER row in font {font} of at most {budget} px"


PARTS = (part_card,)

for part in PARTS:
    part()

print("batch flow: ok")
