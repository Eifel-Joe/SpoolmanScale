"""Read the source tree without a compiler and check the screens around several identical spools made in one go (n10, V44): a create without a tag (X) can make up to ten spools that are alike. The rules (the count, the step after each try, the ids as text) are services/batch_create_rules.h with their own compiled test, test_batch_create_rules.py; this one holds the screens to them. One function per part, in the order of the plan:

  part_card()   the count on both create cards, only in the flag, and the create button naming it
  part_loop()   the run: the spools one after another, the stop at the first failure, one filament
  part_end()    after the run: one overview, one location for all, all labels one after another
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
POPUP_H = "ui/tag_create_popup.h"
NOTAG = "ui/no_tag_create.cpp"
NOTAG_H = "ui/no_tag_create.h"
COPY = "ui/copy_flow.cpp"
INTERNAL_H = "ui/spool_flow_internal.h"
INFO = "ui/more_info_screen.cpp"
INFO_H = "ui/more_info_screen.h"
PRINTER = "ui/printer_screen.cpp"
PRINTER_H = "ui/printer_screen.h"
SHEET = "ui/print_sheet.cpp"
SHEET_H = "ui/print_sheet.h"
CARD = "ui/print_card.cpp"
CARD_H = "ui/print_card.h"
LOOP = "app/app_loop.cpp"


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


def loops_of(body, where):
    """[(condition, body, end)] of every while and for loop in a body as
    body_of() returns it: the condition without its parentheses, the code
    between the loop's braces, and the offset of its closing brace."""
    found = []
    for m in re.finditer(r"(?<![A-Za-z0-9_])(?:while|for)\(", body):
        depth = 0
        for close in range(m.end() - 1, len(body)):
            if body[close] == "(":
                depth += 1
            elif body[close] == ")":
                depth -= 1
                if depth == 0:
                    break
        assert body[close + 1:close + 2] == "{", f"{where}: a loop without braces"
        end = braces_close(body, close + 1)
        found.append((body[m.end():close], body[close + 2:end], end))
    assert "do{" not in body, f"{where}: a do loop, which loops_of() does not read"
    return found


def string_tables():
    """lang.h, lang.cpp, the French working file by id and the rows of the
    width budgets, for text_row() and budget_never()."""
    work = {}
    for line in (root / "tools" / "lang_fr.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            entry = json.loads(line)
            work[entry["id"]] = entry
    budgets = [line.split("\t") for line in (root / "tools" / "ui_budgets.tsv").read_text(encoding="utf-8").splitlines()
               if line and not line.startswith("#")]
    return {"h": text_of("lang.h"), "cpp": text_of("lang.cpp"), "work": work, "budgets": budgets}


def text_row(tables, sid, german, conv, spec="%d"):
    """The German, English and French of one text, held to the house rules: in
    the enum, three cells in lang.cpp without an em-dash, exactly conv
    conversions in each and all of them spec (%d unless named), German as
    written when german is given, translated in the working file with the
    French of lang.cpp, and a width budget."""
    assert re.search(rf"\b{sid}\b\s*,", tables["h"]), f"src/lang.h: {sid} is not in the enum"
    row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                    + sid + r"\b", tables["cpp"])
    assert row, f"src/lang.cpp: the row of {sid} is not there"
    for lang_name, got in zip(("German", "English", "French"), row.groups()):
        assert got.strip(), f"src/lang.cpp: {sid} has no {lang_name} text"
        assert "\N{EM DASH}" not in got, f"src/lang.cpp: {sid} ({lang_name}) has an em-dash"
        assert got.count("%") == conv and got.count(spec) == conv, \
            f"src/lang.cpp: {sid} ({lang_name}) '{got}' does not have exactly {conv} {spec}"
    if german is not None:
        assert row.group(1) == german, f"src/lang.cpp: {sid} reads '{row.group(1)}' in German, expected '{german}'"
    entry = tables["work"].get(sid)
    assert entry, f"tools/lang_fr.jsonl: {sid} is missing"
    assert entry["st"] == "ok" and entry["fr"] == row.group(3), \
        f"tools/lang_fr.jsonl: {sid} is not translated, or not the French of lang.cpp"
    assert any(b[0] == sid for b in tables["budgets"]), f"tools/ui_budgets.tsv: {sid} has no width budget"
    return row.groups()


def budget_never(tables, sid, font, budget):
    """sid has a NEVER row in ui_budgets.tsv, in that font, of at most budget px."""
    rows = [b for b in tables["budgets"] if b[0] == sid]
    assert any(b[1] == font and int(b[2]) <= budget and b[3] == "NEVER" for b in rows), \
        f"tools/ui_budgets.tsv: {sid} has no NEVER row in font {font} of at most {budget} px"


def callers_of(name):
    """The files under src/ whose code (comments and strings blanked) calls name."""
    return sorted(str(p.relative_to(src)).replace("\\", "/") for p in src.rglob("*")
                  if p.suffix in (".cpp", ".h") and re.search(rf"(?<![A-Za-z0-9_]){name}\(",
                                                              cc.Source(p.read_text(encoding="utf-8")).code))


# ------------------------------------------------------------
#  Part 1: the count on the cards (Task 7)
# ------------------------------------------------------------

def part_card():
    """Both create cards get the count at their top right, only in the flag;
    a tap opens the price pad with an empty field; an emptied field keeps the
    count, a valid answer is taken and the card shows it on its create button,
    anything else typed changes nothing and shows the range. The
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

    # 3. The tap opens the price pad with an empty field, under its own title
    # and grey line: the pad fills a value in as "%.2f" and then refuses
    # another digit, so the count filled in ("1.00") would have to be deleted
    # before a 3 could be typed. The answer goes to a function of this file.
    pad = "showPricePadInfo(0,STR_BATCH_COUNT_TITLE,STR_BATCH_COUNT_INFO,"
    tap = handler_of(BATCH, "showPricePadInfo(")
    assert pad in tap, f"src/{BATCH}: the tap does not open the price pad empty as {pad}...)"
    answer = re.search(re.escape(pad) + r"([A-Za-z_]\w*)\)", tap)
    assert answer, f"src/{BATCH}: the tap does not hand the pad's answer to a function"
    body = body_of(BATCH, answer.group(1))
    where = f"src/{BATCH}: {answer.group(1)}()"
    # Only a card that still stands, and only in the flag: a count typed for a
    # card that was closed meanwhile reaches no create.
    assert "noTagMode()" in body[:body.find("batchCountValid(")], \
        f"{where}: takes a count without asking noTagMode() first"
    # An emptied field (0) types nothing, as for the fill weight: the count
    # stays, and the range is not shown for it.
    emptied = re.search(r"\w+<=0(?:\.0f)?\)return;", body)
    assert emptied and body.find(emptied.group(0)) < body.find("batchCountValid("), \
        f"{where}: an emptied field (0) does not leave the count alone before the range is checked"
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
    tables = string_tables()
    ranges = (f"{lo} bis {hi}", f"{lo} to {hi}", f"{lo} à {hi}")
    for sid, german, conv, in_range in (
        ("STR_BATCH_COUNT_CHIP", "Anzahl %d", 1, False),
        ("STR_BATCH_COUNT_TITLE", None, 0, False),
        ("STR_BATCH_COUNT_INFO", None, 0, True),
        ("STR_BATCH_COUNT_RANGE", "1 bis 10", 0, True),
        ("STR_BATCH_CREATE_N", "%d anlegen", 1, False),
    ):
        row = text_row(tables, sid, german, conv)
        for lang_name, got, span in zip(("German", "English", "French"), row, ranges):
            if in_range:
                assert span in got, f"src/lang.cpp: {sid} ({lang_name}) '{got}' does not name the range '{span}'"

    # The button's two texts fit it by the house button rule (4 px of air each
    # side), the create button's the narrowest answer beside the price
    # (TCP_ANSWER_W, COPY_CARD_ANSWER_W: 140 px, less 8).
    for sid, font, budget in (("STR_BATCH_COUNT_CHIP", "14", chip_w - 8), ("STR_BATCH_COUNT_RANGE", "14", chip_w - 8),
                              ("STR_BATCH_CREATE_N", "18", 132)):
        budget_never(tables, sid, font, budget)


# ------------------------------------------------------------
#  Part 2: the spools one after another (Task 8)
# ------------------------------------------------------------

def part_loop():
    """With a count above one, and only in the flag, both creates hand over to
    a run in batch_create.cpp: one loop that makes the spools one after
    another, shows "Spool 2 of 5 ..." before each call, sends every call
    through serverReachNote(), asks batchCreateStep() after each try and goes
    on only on BS_NEXT. A copy keeps its loaded template for the whole run and
    frees it once afterwards; a spool from the card makes its filament once and
    hangs every further spool on it. What a copy left behind, or a price the
    server did not keep, is said once at the end. Nothing made: each path ends
    as one failed create ends today. The texts are in the tables."""

    # 1. The interfaces as the plan names them.
    header = code_of(BATCH_H)
    for decl in ("void batchCopyCreate(int template_spool_id, int template_filament_id, const SpoolStart& start,"
                 " float tare_g, float chosen_core_g);",
                 "void batchTagCreate(const TagCreateInput& in, TagFilamentPlan* plan, const TagSpoolValues& values,"
                 " const char* vendor);"):
        assert squeeze(decl) in header, f"src/{BATCH_H}: does not declare {decl}"
    assert squeeze("bool tagCreateAnswerLost(int code);") in code_of(POPUP_H), \
        f"src/{POPUP_H}: does not declare bool tagCreateAnswerLost(int code);"
    assert squeeze("void noTagBatchFinish(const int* ids, int made, int wanted, const char* vendor,"
                   " const char* reason);") in code_of(NOTAG_H), f"src/{NOTAG_H}: does not declare noTagBatchFinish()"

    # 2. The way in: only in the flag and with a count above one, after what
    # every spool shares is worked out and before the one create of today,
    # which a count of one still takes unchanged. Nothing else calls the runs.
    gate = "if(noTagMode()&&batchCount()>1){"
    in_order(body_of(COPY, "doCopySpoolCreate"),
             ["if(!wifi_ok){copyBbsClear();return;}", "constSpoolStartstart=spoolStartValues(",
              gate + "batchCopyCreate(template_spool_id,template_filament_id,start,tare_g,chosen_core_g);return;}",
              "serverReachNote(backendCreateSpool("], f"src/{COPY}: doCopySpoolCreate()")
    in_order(body_of(POPUP, "runCreate"),
             ["if(!wifi_ok){runPlan();return;}", "if(!planCanCreate(s_plan.state)||sortTwinPopupOpen())return;",
              "constTagSpoolValuesvalues=",
              gate + "batchTagCreate(s_in,&s_plan,values,s_in.vendor);return;}",
              "serverReachNote(backendCreateFromTag("], f"src/{POPUP}: runCreate()")
    for name, rel in (("batchCopyCreate", COPY), ("batchTagCreate", POPUP)):
        assert callers_of(name) == sorted([BATCH, BATCH_H, rel]), \
            f"src/: {name}() is named in {callers_of(name)}, expected only {rel} besides its own unit"

    # 3. Both runs: one loop, the progress before the call, the call through
    # serverReachNote() and nowhere else in the run, the step asked after each
    # try and the loop going on only on BS_NEXT. The overlay comes down after
    # the loop, before anything else is shown; the cache forgets once; a stop
    # gets its reason; nothing made does not reach the finish, which would show
    # ids[0].
    progress = body_of(BATCH, "runProgress")
    for needle in ("STR_BATCH_PROGRESS", "loadingOverlayShow("):
        assert needle in progress, f"src/{BATCH}: runProgress() does not use {needle}"
    reason = body_of(BATCH, "runStopReason")
    for needle in ("BS_STOP_UNSURE", "STR_BATCH_STOP_UNSURE", "STR_BATCH_STOP_FAILED"):
        assert needle in reason, f"src/{BATCH}: runStopReason() does not use {needle}"
    assert "BATCH_COUNT_MAX" in body_of(BATCH, "runWanted"), \
        f"src/{BATCH}: runWanted() does not keep the count within the ids (BATCH_COUNT_MAX)"
    runs = {}
    for name, call, ok, made_id, unsure in (
        ("batchCopyCreate", "backendCreateSpool(", "constboolok=(code==200||code==201)&&new_id>0;",
         "ids[made++]=new_id;", "batchCreateStep(made,wanted,ok,tagCreateAnswerLost(code))"),
        ("batchTagCreate", "backendCreateFromTag(", "constboolok=(code==200||code==201)&&r.spool_id>0;",
         "ids[made++]=r.spool_id;", "batchCreateStep(made,wanted,ok,r.spool_sent&&tagCreateAnswerLost(code))"),
    ):
        body = body_of(BATCH, name)
        where = f"src/{BATCH}: {name}()"
        loops = loops_of(body, where)
        assert len(loops) == 1, f"{where}: {len(loops)} loops, expected one"
        cond, inner, end = loops[0]
        assert cond == "step==BS_NEXT", f"{where}: the loop goes on while '{cond}', expected step == BS_NEXT"
        assert body.count(call) == 1 and inner.count("serverReachNote(" + call) == 1, \
            f"{where}: {call}...) is not called once, in the loop, through serverReachNote()"
        in_order(inner, ["runProgress(made+1,wanted);", "serverReachNote(" + call, ok, made_id, "step=" + unsure],
                 f"{where}: the loop")
        assert re.search(r"intids\[BATCH_COUNT_MAX\](?:=\{\})?;", body) and \
            "constintwanted=runWanted();" in body[:body.find("while(")], \
            f"{where}: the ids are not BATCH_COUNT_MAX long, or the count is not runWanted()"
        after = body[end + 1:]
        for banned in ("loadingOverlayHide(", "spoolCacheForget(", "noTagBatchFinish(", "copyBbsClear("):
            assert banned not in inner, f"{where}: {banned}) inside the loop"
        unconditional_in(after, "loadingOverlayHide();", where + " after the loop")
        assert body.count("spoolCacheForget(") == 1, f"{where}: the cache does not forget exactly once"
        in_order(after, ["if(made==0){", "return;}", "spoolCacheForget(",
                         "if(step!=BS_DONE)runStopReason(step,made+1,code,reason,sizeof(reason));",
                         "noTagBatchFinish(ids,made,wanted,"], where + " after the loop")
        assert after.find("loadingOverlayHide();") < after.find("if(made==0){"), \
            f"{where}: the overlay is still up when nothing was made"
        runs[name] = (body, inner, after, where)

    # 4. The copy: the template loaded for the card serves every copy
    # (copyBbsCreate() reads again only for another template) and is freed once,
    # on every way out of the run; what each copy left behind is gathered and
    # said once. Nothing made: the end of a copy that failed, as today.
    body, inner, after, where = runs["batchCopyCreate"]
    assert body.count("copyBbsClear();") == 1, f"{where}: copyBbsClear() is not called exactly once"
    unconditional_in(after, "copyBbsClear();", where + " after the loop")
    assert "if(spoolTypesActive())missing|=copyBbsLastMissing();" in inner, \
        f"{where}: the parts each copy left behind are not gathered as in doCopySpoolCreate()"
    failed = after[after.find("if(made==0){"):]
    failed = failed[:failed.find("return;}")]
    for needle in ("linkPickerClose();", "T(STR_COPY_FAIL)", "UI_COL_BAD_TEXT"):
        assert needle in failed, f"{where}: nothing made does not end as a failed copy ({needle})"
    in_order(after, ["noTagBatchFinish(ids,made,wanted,copyCardVendor(),reason);",
                     "if(missing!=CM_NONE&&noTagYellowLineAllowed(ids[0],", "copyCreatedStatus(missing);"], where)
    assert not re.search(r"\bstatic\s+void\s+copyCreatedStatus\(", text_of(COPY)), \
        f"src/{COPY}: copyCreatedStatus() is still static"
    assert "voidcopyCreatedStatus(uint8_tmissing);" in code_of(INTERNAL_H), \
        f"src/{INTERNAL_H}: does not declare copyCreatedStatus()"

    # 5. The card: after a spool that made its filament, the plan is that
    # filament (TFS_FOUND), so every further spool hangs on it; its weights stay,
    # so every spool is weighed against the first one's empty spool. A price the
    # server did not keep is said once. Nothing made: the card says why, as for
    # one spool, and stays; else it closes before the finish.
    body, inner, after, where = runs["batchTagCreate"]
    in_order(inner, ["ids[made++]=r.spool_id;", "price_lost|=r.price_lost;",
                     "if(r.filament_id>0){plan->state=TFS_FOUND;plan->filament_id=r.filament_id;}",
                     "step=batchCreateStep("], where + ": the loop")
    assert body.count("plan->") == 2 and "found_spool_weight_g" not in body, \
        f"{where}: the plan is changed beyond its state and filament"
    assert "if(made==0){tagCreateFailed(code,r);return;}" in after, \
        f"{where}: nothing made does not leave the card saying why (tagCreateFailed())"
    in_order(after, ["closeTagCreatePopup();", "noTagBatchFinish(ids,made,wanted,vendor,reason);",
                     "if(price_lost&&noTagYellowLineAllowed(ids[0],", "statusMessageShow(T(STR_TAGNEW_PRICE_LOST)"],
             where)
    assert body.count("STR_TAGNEW_PRICE_LOST") == 1, f"{where}: the lost price is said more than once"

    # Every backend makes no filament for a plan that has one (TFS_FOUND):
    # Spoolman direct, FilaMan and Bambuddy behind Spoolman go through
    # filamentForSpool(), which answers before it would create one; Bambuddy
    # with its own inventory has no filament and never reports one, so its
    # plan stays as it is.
    api = "services/backend_api.cpp"
    in_order(body_of(api, "filamentForSpool"),
             ["*out_id=plan.filament_id;", "if(plan.state==TFS_FOUND)return200;", "CreateTagFilament("],
             f"src/{api}: filamentForSpool()")
    in_order(body_of(api, "backendCreateFromTag"),
             ["bambuddyCreateFromTag(", "bambuddyCreateUnderSpoolman(", "filamentForSpool("],
             f"src/{api}: backendCreateFromTag()")
    assert "filamentForSpool(" in body_of(api, "bambuddyCreateUnderSpoolman"), \
        f"src/{api}: bambuddyCreateUnderSpoolman() does not take its filament from filamentForSpool()"
    assert "filament_id" not in body_of(api, "bambuddyCreateFromTag"), \
        f"src/{api}: bambuddyCreateFromTag() reports a filament"

    # 6. The card's own end of a create that made nothing, shared with the run;
    # the lost answer under its exported name only.
    code = code_of(POPUP)
    assert "tagCreateFailed(code,r);" in body_of(POPUP, "runCreate"), \
        f"src/{POPUP}: runCreate() does not end a failed create with tagCreateFailed()"
    failed = body_of(POPUP, "tagCreateFailed")
    in_order(failed, ["if(r.spool_sent&&tagCreateAnswerLost(code)){", "STR_TAGNEW_SPOOL_UNSURE", "return;}",
                      "if(r.filament_id>0){", "s_plan.state=TFS_FOUND;", "STR_TAGNEW_FILAMENT_ONLY",
                      "s_plan_stale=true;", "STR_TAGNEW_CREATE_FAIL", "showStatus(buf,UI_COL_BAD_TEXT,true);"],
             f"src/{POPUP}: tagCreateFailed()")
    assert not re.search(r"(?<![A-Za-z0-9_])answerLost\(", code), f"src/{POPUP}: answerLost() is still there"
    assert "booltagCreateAnswerLost(intcode){" in code, f"src/{POPUP}: tagCreateAnswerLost() is not defined"

    # 7. The end of the run: the first spool shown as after one create, never
    # with nothing made, and the green line naming how many when there are
    # several, with the Bambu chip hint when that stands. What follows it (the
    # overview, the location, the labels) is part_end().
    body = body_of(NOTAG, "noTagBatchFinish")
    in_order(body, ["if(!ids||made<1)return;", "noTagCreateFinish(ids[0],vendor);", "if(made<2)return;",
                    "s_chip_hint?STR_BATCH_CREATED_BAMBU:STR_BATCH_CREATED", "statusMessageShow("],
             f"src/{NOTAG}: noTagBatchFinish()")

    # 8. The texts. "Spool 2 of 5" on the loading overlay, its label 290 px at
    # 16; the green lines on the status line like STR_NOTAG_CREATED; the
    # reasons one line of a card's text width. A lost answer sends the user to
    # the inventory, as the card does for one spool.
    tables = string_tables()
    for sid, conv, font, budget in (("STR_BATCH_PROGRESS", 2, "16", 290),
                                    ("STR_BATCH_CREATED", 1, "14", 269),
                                    ("STR_BATCH_CREATED_BAMBU", 1, "14", 269),
                                    ("STR_BATCH_STOP_FAILED", 2, "16", 360),
                                    ("STR_BATCH_STOP_UNSURE", 1, "16", 360)):
        row = text_row(tables, sid, None, conv)
        budget_never(tables, sid, font, budget)
        if sid == "STR_BATCH_STOP_UNSURE":
            for got, word in zip(row, ("Inventar", "inventory", "inventaire")):
                assert word in got, f"src/lang.cpp: {sid} '{got}' does not send the user to the inventory ({word})"
    assert "lv_obj_set_width(lbl_loading,290);" in code_of("ui/loading_overlay.cpp"), \
        "src/ui/loading_overlay.cpp: the overlay's line is no longer 290 px wide"


# ------------------------------------------------------------
#  Part 3: one overview, one location for all, all labels (Task 9)
# ------------------------------------------------------------

def block_after(body, head, where):
    """The code between the braces of the one block that starts with head (it
    ends in an opening brace), blanks gone, and the offset of its closing brace."""
    head = squeeze(head)
    assert head.endswith("{"), f"{where}: '{head}' must end in an opening brace"
    assert body.count(head) == 1, f"{where}: '{head}' is not there exactly once"
    opening = body.find(head) + len(head) - 1
    end = braces_close(body, opening)
    return body[opening + 1:end], end


def part_end():
    """After a run with at least one spool made, the scale shows one overview
    with every new number (and why it stopped), a pass after the end and only
    for the first spool still on the screen; its "Next" opens the location
    picker for that spool as after one create. A location chosen there is
    written to every other spool of the run, one call each with "Location 2 of
    5 ..." on the overlay, stopping at the first failure; closed without a
    choice, nothing. Then "5 labels?" (only with a printer ready); "Print"
    prints every label one after another from the first spool's data with only
    the number changed, the next only once the print card has closed, and a
    result other than printed or sent stops the rest with their numbers in
    yellow. With the preview the sheet comes once for the first, and its print
    goes on with the rest. The card names the label's place, the loop drives
    it, the create's busy predicate covers every step, the texts are in the
    tables."""

    # 1. The interfaces.
    for rel, decls in (
        (BATCH_H, ("void batchRunKeep(const int* ids, int n);", "int batchRunOf(int first_id, const int** ids);",
                   "void batchLocationToRest(int first_id, const char* name);",
                   "void batchLabelsAfterSheet(int first_id);", "void batchLabelsStart(LabelKind kind, int from);",
                   "void batchLabelsTick();", "void batchLabelsForget();", "bool batchLabelsBusy();")),
        (INFO_H, ("bool locationPickerChosen(char* name, size_t cap, bool* cleared);",)),
        (PRINTER_H, ("LabelPrintResult printerPrintSpoolData(const SpoolLabelData& spool, LabelKind kind);",)),
        (CARD_H, ("void printCardSetStep(int i, int n);",)),
        (SHEET_H, ("bool printSheetOpen();",)),
    ):
        header = code_of(rel)
        for decl in decls:
            assert squeeze(decl) in header, f"src/{rel}: does not declare {decl}"

    # 2. The finish: the first spool as after one create, then the run kept
    # (its ids and reason live only for the call), and the picker the ending
    # armed turned into the overview, which comes before it. None of that
    # under a condition: the overview is armed exactly when the picker would
    # have been, for the first spool still on the screen.
    where = f"src/{NOTAG}: noTagBatchFinish()"
    body = body_of(NOTAG, "noTagBatchFinish")
    in_order(body, ["if(!ids||made<1)return;", "noTagCreateFinish(ids[0],vendor);", "batchRunKeep(ids,made);",
                    "s_over_wanted=wanted;", 'snprintf(s_over_reason,sizeof(s_over_reason),"",reason?reason:"");',
                    "s_over_for=s_loc_for;", "s_over_wait=s_loc_wait;", "s_loc_for=0;", "s_loc_wait=false;",
                    "if(made<2)return;"], where)
    head = body[:body.find("if(made<2)return;")]
    head = head[head.find("noTagCreateFinish("):]
    assert head.count("{") == head.count("}") == 0 and "return" not in head, \
        f"{where}: the run is kept or the overview armed under a condition"
    keep = body_of(BATCH, "batchRunKeep")
    assert "BATCH_COUNT_MAX" in keep and "s_run_n=" in keep, \
        f"src/{BATCH}: batchRunKeep() does not keep the ids within BATCH_COUNT_MAX"
    run_of = body_of(BATCH, "batchRunOf")
    assert "s_run_ids[0]==first_id" in run_of, f"src/{BATCH}: batchRunOf() does not key the run by its first spool"

    # 3. The overview: a pass after it was armed (what the ending released goes
    # back to the pool only after LVGL ran), only for the spool still shown;
    # all its numbers as one text, "5 made" or "3 of 5 made", the reason of a
    # stop in red, and one button whose tap only parks "Next".
    where = f"src/{NOTAG}: askOverview()"
    in_order(body_of(NOTAG, "askOverview"),
             ["if(s_over_wait){s_over_wait=false;return;}", "constintid=s_over_for;", "s_over_for=0;",
              "if(!shownStill(id)){", "return;}", "buildOverview(id);"], where)
    where = f"src/{NOTAG}: buildOverview()"
    body = body_of(NOTAG, "buildOverview")
    for needle in ("batchRunOf(spool_id,&ids)", "closeNoTagLabelAsk();", "s_card_for=spool_id;",
                   "batchIdsText(ids,made,list,sizeof(list))", "T(STR_BATCH_OVERVIEW_SOME),made,s_over_wanted",
                   "T(STR_BATCH_OVERVIEW_ALL),made", "s_over_reason", "UI_COL_BAD_TEXT",
                   "askButton(box,UI_CARD_ROW_X,true,STR_BTN_NEXT)", "lv_scr_act()"):
        assert needle in body, f"{where}: does not do {needle}"
    assert body.find("closeNoTagLabelAsk();") < body.find("s_card_for=spool_id;"), \
        f"{where}: the card's spool is set before the old card is closed"
    assert handler_of(NOTAG, "LA_NEXT") == "s_answer=LA_NEXT;", \
        f"src/{NOTAG}: the overview's button does more than park its answer"

    # 4. The loop: the overview's "Next" arms the picker for its spool, a pass
    # later, only while that spool is still shown; "Print" for a run goes to
    # the run, for one spool the way it always went. The steps after the card
    # in the order of the way: picker, overview, the location to the rest,
    # then the question for a label.
    where = f"src/{NOTAG}: noTagLabelAskTick()"
    body = body_of(NOTAG, "noTagLabelAskTick")
    inner, end = block_after(body, "if (s_card) {", where)
    in_order(inner, ["if(answer==LA_NONE&&shownStill(s_card_for))return;",
                     "constboolnext=answer==LA_NEXT&&shownStill(s_card_for);", "constintasked=s_card_for;",
                     "closeNoTagLabelAsk();", "if(next){s_loc_for=asked;s_loc_wait=true;return;}",
                     "if(print&&batchRunOf(asked,nullptr)>1){printRun(asked);return;}", "if(print){"], where)
    in_order(body[end:], ["if(s_loc_for){askLocation();return;}", "if(s_over_for){askOverview();return;}",
                          "if(s_rest_for){writeRest();return;}", "if(!s_ask_for)return;"], where + " after the card")
    where = f"src/{NOTAG}: printRun()"
    body = body_of(NOTAG, "printRun")
    assert body == squeeze("if (labelPrinterAskFirst()) { batchLabelsAfterSheet(first_id); show_print_sheet_pending = true; }"
                           " else batchLabelsStart(LABEL_KIND_SPOOL, 0);"), \
        f"{where}: with the preview the sheet for the first, else every label in a row from the first"

    # 5. The question for a label names the count for a run.
    body = body_of(NOTAG, "buildAsk")
    for needle in ("batchRunOf(spool_id,nullptr)", "T(STR_BATCH_LABEL_ASK)", "T(STR_LABEL_ASK)"):
        assert needle in body, f"src/{NOTAG}: buildAsk() does not use {needle}"

    # 6. The location. The picker's end hands its choice to the rest of the run
    # only when it wrote one, for the first spool still shown (else the picker
    # wrote whatever took the screen); the question for a label is armed after
    # it as before, and the rest is written a pass later, before the question.
    where = f"src/{NOTAG}: locationPickerEnded()"
    body = body_of(NOTAG, "locationPickerEnded")
    cond = re.search(r"if\(([^{]*)\)\{s_rest_for=spool_id;s_rest_clear=cleared;s_rest_wait=true;\}", body)
    assert cond, f"{where}: the rest of the run is not armed as one block under one condition"
    assert "||" not in cond.group(1) and all(part in cond.group(1).split("&&") for part in (
        "shownStill(spool_id)", "batchRunOf(spool_id,nullptr)>1",
        "locationPickerChosen(s_rest_loc,sizeof(s_rest_loc),&cleared)")), \
        f"{where}: the rest is armed without a written choice for the first spool still shown: {cond.group(1)}"
    in_order(body, ["locationPickerChosen(", "s_rest_for=spool_id;",
                    "s_ask_for=(shownStill(spool_id)&&labelPrinterReadiness()==LP_OK)?spool_id:0;",
                    "s_ask_wait=s_ask_for!=0;"], where)
    assert code_of(NOTAG).count("s_rest_for=spool_id;") == 1, f"src/{NOTAG}: the rest is armed in more than one place"
    in_order(body_of(NOTAG, "writeRest"),
             ["if(s_rest_wait){s_rest_wait=false;return;}", "constintid=s_rest_for;", "s_rest_for=0;",
              "batchLocationToRest(id,s_rest_clear?nullptr:s_rest_loc);"], f"src/{NOTAG}: writeRest()")

    # The picker's side: true only when the last end wrote a location (every
    # request starts without, the write's answer decides), the name and
    # whether it was "no location" from what it wrote.
    where = f"src/{INFO}: locationPickerChosen()"
    body = body_of(INFO, "locationPickerChosen")
    in_order(body, ["if(!s_loc_chosen)returnfalse;", "loc_patch_name", "*cleared=loc_patch_name[0]=='';",
                    "returntrue;"], where)
    assert "s_loc_chosen=false;" in body_of(INFO, "requestLocationPicker"), \
        f"src/{INFO}: a new request keeps the choice of the last picker"
    patch, _ = block_after(body_of(INFO, "handleMoreInfoDeferredActions"), "if (loc_patch_pending) {", where)
    written, _ = block_after(patch, "if (target > 0) {", where)
    in_order(written, ["serverReachNote(backendPatchSpoolLocation(", "s_loc_chosen=backendWriteOk(code);"], where)
    writers = sorted(name for name, start, end in cc.find_functions(source_of(INFO).code)
                     if re.search(r"\bs_loc_chosen=", squeeze(source_of(INFO).code[start + 1:end])))
    assert writers == ["handleMoreInfoDeferredActions", "requestLocationPicker"], \
        f"src/{INFO}: s_loc_chosen is written by {writers}"

    # The write to the rest: one loop from the second spool, "Location 2 of 5
    # ..." before each call, the call through serverReachNote(), the stop at
    # the first failure, the overlay down after the loop, and the spools left
    # without in yellow, unless the Bambu chip hint stands.
    where = f"src/{BATCH}: batchLocationToRest()"
    body = body_of(BATCH, "batchLocationToRest")
    loops = loops_of(body, where)
    assert len(loops) == 1, f"{where}: {len(loops)} loops, expected one"
    _, inner, end = loops[0]
    assert "intat=1;" in body[:body.find("for(")], f"{where}: the loop does not start at the second spool"
    in_order(inner, ["T(STR_BATCH_LOC_PROGRESS),at+1,n", "loadingOverlayShow(",
                     "serverReachNote(backendPatchSpoolLocation(cfg_spoolman_base,ids[at],name,",
                     "if(!backendWriteOk(code))break;"], where + ": the loop")
    after = body[end + 1:]
    unconditional_in(after, "loadingOverlayHide();", where + " after the loop")
    in_order(after, ["if(at>=n)return;", 'runLeftOut(ids,at,n,STR_BATCH_LOC_MISSING,"");'], where + " after the loop")

    # 7. The busy predicate covers the overview (armed; up is the card), the
    # rest's location armed, and the labels running; closing the screens
    # forgets an overview not yet shown, as it forgets the picker.
    busy = body_of(NOTAG, "noTagAfterCreateBusy")
    for needle in ("s_over_for!=0", "s_rest_for!=0", "batchLabelsBusy()"):
        assert needle in busy, f"src/{NOTAG}: noTagAfterCreateBusy() does not cover {needle}"
    assert body_of(BATCH, "batchLabelsBusy") == "returns_lr==LR_RUN;", \
        f"src/{BATCH}: batchLabelsBusy() must be true while labels are being printed, and only then"
    close = body_of(NOTAG, "closeNoTagLabelAsk")
    for needle in ("s_over_for=0;", "s_over_wait=false;"):
        assert needle in close, f"src/{NOTAG}: closeNoTagLabelAsk() does not do {needle}"

    # 8. The print with data instead of the screen, and its result.
    assert re.search(r"\bstatic\s+LabelPrintResult\s+printSpoolLabel\(", text_of(PRINTER)), \
        f"src/{PRINTER}: printSpoolLabel() does not hand its result back"
    assert body_of(PRINTER, "printSpoolLabel").endswith("returnresult;"), \
        f"src/{PRINTER}: printSpoolLabel() does not end in return result"
    assert body_of(PRINTER, "printerPrintSpoolData") == squeeze("printerOffsetFlush(); return printSpoolLabel(spool, false, kind);"), \
        f"src/{PRINTER}: printerPrintSpoolData() is not the spool's label from the data handed in"

    # 9. The labels. The start takes the first spool's data from the screen,
    # once; from the sheet only when the sheet was the run's, and only when the
    # sheet's own label went out. The tick waits for the print card to close,
    # changes only the number, names the place on the card, prints, and stops
    # at a result other than printed or sent with the rest's numbers in yellow.
    where = f"src/{BATCH}: batchLabelsStart()"
    body = body_of(BATCH, "batchLabelsStart")
    in_order(body, ["constboolsheet=from>0;", "if(sheet&&s_lr!=LR_SHEET)return;", "labelSpoolFromScan(&d)",
                    "batchRunOf(d.id,&ids)", "if(sheet&&!labelWentOut(printerLastLabelResult())){",
                    'runLeftOut(ids,0,n,STR_BATCH_LABELS_MISSING,"");', "s_lr_data=d;", "s_lr_next=from;", "s_lr=LR_RUN;"], where)
    # After the sheet's print the screen no longer shows the run's first spool:
    # the rest is dropped and said in yellow like every other stop, before the
    # run of the spool now shown is asked for.
    left, _ = block_after(body, "if (sheet && (!shown || d.id != sheet_for)) {", where)
    in_order(left, ["batchRunOf(sheet_for,&rest)", 'if(rn>1)runLeftOut(rest,1,rn,STR_BATCH_LABELS_MISSING,"");',
                    "return;"], where + ": the sheet's spool left the screen")
    assert body.find("if(sheet&&(!shown||d.id!=sheet_for)){") < body.find("batchRunOf(d.id,&ids)"),         f"{where}: the run of the spool now shown is asked before the sheet's spool is checked"
    assert body_of(BATCH, "labelWentOut") == squeeze(
        "return text == labelPrintResultString(LP_OK) || text == labelPrintResultString(LP_NET_SENT);"), \
        f"src/{BATCH}: labelWentOut() is not 'printed or sent' as the print's own verdict text"
    where = f"src/{BATCH}: batchLabelsTick()"
    body = body_of(BATCH, "batchLabelsTick")
    in_order(body, ["if(s_lr==LR_SHEET){", "if(!show_print_sheet_pending&&!printSheetOpen()){", "labelsIdle();",
                    "return;}", "if(s_lr!=LR_RUN||printCardOpen())return;",
                    "if(s_lr_next>0&&!labelWentOut(printerLastLabelResult())){", "labelsIdle();",
                    'if(s_lr_next<n)runLeftOut(ids,s_lr_next,n,STR_BATCH_LABELS_MISSING,"");', "return;}",
                    "if(s_lr_next>=n){", "labelsIdle();", "return;}", "s_lr_data.id=ids[s_lr_next];",
                    "printCardSetStep(s_lr_next+1,n);",
                    "constLabelPrintResultr=printerPrintSpoolData(s_lr_data,s_lr_kind);", "printCardSetStep(0,0);",
                    "if(r!=LP_OK&&r!=LP_NET_SENT){", "labelsIdle();", 'runLeftOut(ids,s_lr_next,n,STR_BATCH_LABELS_MISSING,"");', "return;}",
                    "s_lr_next++;"], where)
    # A Brother's reply after a label of the run (its card waits for OK, and
    # the label's kept verdict becomes the reply) stops the run as a failed
    # print does: once that card has closed the verdict of the label before is
    # asked, the sheet's included, and only then is the run called done. No
    # label is counted done in the pass it was sent.
    tail = body[body.find("constLabelPrintResultr=printerPrintSpoolData("):]
    assert "labelsIdle();" not in tail[tail.find("return;}") + len("return;}"):],         f"{where}: the run ends in the pass of its last label, before a reply to it could be seen"
    assert body.count("printerPrintSpoolData(") == 1 and code_of(BATCH).count("printerPrintSpoolData(") == 1, \
        f"src/{BATCH}: a label is printed elsewhere than in the tick"
    # Both leave-outs, the location's and the labels', say which spools in one
    # text, in the log and, unless the Bambu chip hint stands, in yellow.
    in_order(body_of(BATCH, "runLeftOut"),
             ["batchIdsText(ids+from,n-from,list,sizeof(list))", "logSDf(", "if(!noTagYellowLineAllowed(ids[0],",
              "T(text_id),list", "statusMessageShow(msg,UI_COL_WARN);"], f"src/{BATCH}: runLeftOut()")
    idle = body_of(BATCH, "labelsIdle")
    assert "s_lr=LR_IDLE;" in idle and "printCardSetStep(0,0);" in idle, \
        f"src/{BATCH}: labelsIdle() does not end the labels and take the place off the card"
    assert "labelsIdle();" in body_of(BATCH, "batchLabelsForget"), f"src/{BATCH}: batchLabelsForget() does not end the labels"
    in_order(body_of(BATCH, "batchLabelsAfterSheet"), ["if(n<2)return;", "s_lr=LR_SHEET;", "s_lr_first=first_id;",
                                                       "printCardSetStep(1,n);"], f"src/{BATCH}: batchLabelsAfterSheet()")

    # 10. The sheet: its print goes on with the rest; closed by its own buttons
    # without printing, the rest goes; any other close the tick sees.
    where = f"src/{SHEET}: printSheetTick()"
    body = body_of(SHEET, "printSheetTick")
    printing, _ = block_after(body, "if (print_sheet_print_pending) {", where)
    in_order(printing, ["constLabelKindkind=s_kind;", "closePrintSheet();", "printerPrintLastLabel(kind);",
                        "batchLabelsStart(kind,1);"], where)
    for head in ("if (print_sheet_close_pending) {", "if (print_sheet_printer_pending) {"):
        inner, _ = block_after(body, head, where)
        in_order(inner, ["closePrintSheet();", "batchLabelsForget();"], f"{where}: {head}")
    assert body_of(SHEET, "printSheetOpen") == "returns_scr!=nullptr;", f"src/{SHEET}: printSheetOpen() is not the sheet's own pointer"

    # 11. The card: the place of the label in the run, only for two or more.
    body = body_of(CARD, "printCardSetStep")
    assert body == squeeze("s_step_i = i; s_step_n = n;"), f"src/{CARD}: printCardSetStep() does more than keep the place"
    step, _ = block_after(body_of(CARD, "buildCard"), "if (s_step_n >= 2) {", f"src/{CARD}: buildCard()")
    assert "T(STR_BATCH_LABEL_STEP),s_step_i,s_step_n" in step, f"src/{CARD}: the card's place is not STR_BATCH_LABEL_STEP"

    # 12. The loop drives the labels, once a pass, beside the printer's own
    # actions; the print runs from there and never from an event.
    loop = body_of(LOOP, "appLoop")
    assert loop.count("handlePrinterDeferredActions();batchLabelsTick();") == 1, \
        f"src/{LOOP}: batchLabelsTick() does not run right after handlePrinterDeferredActions()"
    assert callers_of("batchLabelsTick") == sorted([LOOP, BATCH, BATCH_H]), \
        f"src/: batchLabelsTick() is called in {callers_of('batchLabelsTick')}"
    assert callers_of("batchLabelsStart") == sorted([BATCH, BATCH_H, NOTAG, SHEET]), \
        f"src/: batchLabelsStart() is called in {callers_of('batchLabelsStart')}"
    assert callers_of("printerPrintSpoolData") == sorted([BATCH, PRINTER, PRINTER_H]), \
        f"src/: printerPrintSpoolData() is called in {callers_of('printerPrintSpoolData')}"
    for rel, needle in ((LOOP, "batchLabelsTick("), (BATCH, "printerPrintSpoolData(")):
        source = source_of(rel)
        for _, _, b0, b1 in cc.handlers(source):
            assert needle not in squeeze(source.code[b0 + 1:b1]), f"src/{rel}: {needle}) in an event handler"
    for rel in (LOOP, SHEET, NOTAG):
        assert '#include "ui/batch_create.h"' in text_of(rel), f"src/{rel}: does not include ui/batch_create.h"

    # 13. The texts: German in the plan's words.
    tables = string_tables()
    for sid, german, conv, spec, font, budget in (
        ("STR_BATCH_OVERVIEW_ALL", "%d Spulen angelegt", 1, "%d", "20", 360),
        ("STR_BATCH_OVERVIEW_SOME", "%d von %d angelegt", 2, "%d", "20", 360),
        ("STR_BATCH_LOC_PROGRESS", "Lagerort %d von %d ...", 2, "%d", "16", 290),
        ("STR_BATCH_LOC_MISSING", "Ohne Lagerort: %s", 1, "%s", "14", 269),
        ("STR_BATCH_LABEL_ASK", "%d Etiketten drucken?", 1, "%d", "20", 360),
        ("STR_BATCH_LABEL_STEP", "Etikett %d von %d", 2, "%d", "14", 150),
        ("STR_BATCH_LABELS_MISSING", "Ohne Etikett: %s", 1, "%s", "14", 269),
    ):
        text_row(tables, sid, german, conv, spec)
        budget_never(tables, sid, font, budget)


PARTS = (part_card, part_loop, part_end)

for part in PARTS:
    part()

print("batch flow: ok")
