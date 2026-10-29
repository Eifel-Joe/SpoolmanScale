"""Read the source tree without a compiler and check the screens around several identical spools made in one go (n10, V44): a create without a tag (X) can make up to ten spools that are alike. The rules (the count, the step after each try, the ids as text) are services/batch_create_rules.h with their own compiled test, test_batch_create_rules.py; this one holds the screens to them. One function per part, in the order of the plan:

  part_card()   the count on both create cards, only in the flag, and the create button naming it
  part_loop()   the run: the spools one after another, the stop at the first failure, one filament
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


def text_row(tables, sid, german, conv):
    """The German, English and French of one text, held to the house rules: in
    the enum, three cells in lang.cpp without an em-dash, exactly conv %d in
    each, German as written when german is given, translated in the working
    file with the French of lang.cpp, and a width budget."""
    assert re.search(rf"\b{sid}\b\s*,", tables["h"]), f"src/lang.h: {sid} is not in the enum"
    row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                    + sid + r"\b", tables["cpp"])
    assert row, f"src/lang.cpp: the row of {sid} is not there"
    for lang_name, got in zip(("German", "English", "French"), row.groups()):
        assert got.strip(), f"src/lang.cpp: {sid} has no {lang_name} text"
        assert "\N{EM DASH}" not in got, f"src/lang.cpp: {sid} ({lang_name}) has an em-dash"
        assert got.count("%") == conv and got.count("%d") == conv, \
            f"src/lang.cpp: {sid} ({lang_name}) '{got}' does not have exactly {conv} %d"
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

    # 7. The end of the run, as little as it is for now: the first spool shown
    # as after one create, never with nothing made, and the green line naming
    # how many when there are several, with the Bambu chip hint when that
    # stands.
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


PARTS = (part_card, part_loop)

for part in PARTS:
    part()

print("batch flow: ok")
