"""Read the source tree without a compiler and check that a spool created without a tag is not weighed (X, V35): the start rule of the card, the copy confirmation and the copy's create see 0 g through the one rule (noTagGross), the card does not guess a fill weight off a reading, the list of spool types is asked without a weight (database pick, copy), and a fill weight that is missing is asked for on the price pad before anything is created, with the density asked after it, an out-of-range answer refused and the plan asked again once a valid one is kept."""
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
PICK = "ui/db_pick_core_popup.cpp"
READING = "spoolExtrasBareReading()"
GROSS = "noTagGross(noTagMode(),"


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


def click_handler_of(rel, marker):
    """The body of the one event handler (lambda or named) that contains marker."""
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


def only_through_rules(rel, allowed):
    """The raw reading is read nowhere in the file but in the allowed places
    (squeezed, exact): the rule that sets it to 0 in the flag must not be
    walked around by a second read."""
    code = squeeze(source_of(rel).code)
    for text in allowed:
        assert squeeze(text) in code, f"src/{rel}: '{text}' is not there"
        code = code.replace(squeeze(text), "")
    assert READING not in code, \
        f"src/{rel}: {READING} is read outside the rule of the no-tag mode (noTagGross, noTagTypeAsk)"


# 1. The reading. The card's start rule, the copy confirmation's start values
# and the copy's create take the weight on the dish through noTagGross(), so
# that a roll in its wrapping lying there is no spool. Nothing else in these
# files reads the pad but the question for the spool type (rule below), and
# the card's guess at the fill weight, which the flag never reaches (2).
body = body_of(POPUP, "startGross")
assert body.startswith("return" + GROSS), \
    f"src/{POPUP}: startGross() does not return noTagGross(noTagMode(), ...)"
assert "g_scale_fitted&&scale_ready" in body and body.find(GROSS) < body.find(READING), \
    f"src/{POPUP}: startGross() lost the check of the scale, or reads the pad before the rule"
only_through_rules(POPUP, [
    "return noTagGross(noTagMode(), (g_scale_fitted && scale_ready) ? spoolExtrasBareReading() : 0.0f);",
    "const float netto = spoolExtrasBareReading() - cardCore();",
])

body = body_of(CONFIRM, "buildCopyCard")
assert squeeze("spoolStartValues(noTagGross(noTagMode(), spoolExtrasBareReading()),") in body, \
    f"src/{CONFIRM}: the start values of the card do not take noTagGross(noTagMode(), ...)"

body = body_of(FLOW, "doCopySpoolCreate")
assert squeeze("const float bare = noTagGross(noTagMode(), spoolExtrasBareReading());") in body, \
    f"src/{FLOW}: doCopySpoolCreate() does not take its reading through noTagGross(noTagMode(), ...)"
assert "spoolStartValues(bare," in body, f"src/{FLOW}: doCopySpoolCreate() no longer starts from bare"
only_through_rules(FLOW, ["const float bare = noTagGross(noTagMode(), spoolExtrasBareReading());"])

# 2. labelWeight(): in the flag the tag's own fill weight is the only answer,
# 0 when there is none, and the nominal size is not guessed off the pad. The
# check stands before the first read of the pad. Without the flag the function
# is as it was.
body = body_of(POPUP, "labelWeight")
flag = re.search(r"if\(([^{}]*noTagMode\(\)[^{}]*)\)return(s_in\.net_weight_g);", body)
assert flag, f"src/{POPUP}: labelWeight() does not return s_in.net_weight_g in the flag"
assert body.find(flag.group(0)) < body.find(READING), \
    f"src/{POPUP}: labelWeight() reads the pad before it leaves for the flag"
assert "s_in.net_weight_g>0" in body[:body.find(READING)], \
    f"src/{POPUP}: labelWeight() no longer returns the tag's fill weight when it has one"
assert "NEWTAG_LABEL_CHOICES" in body, f"src/{POPUP}: labelWeight() lost the guess for a spool with a tag"

# 3. The list of spool types, asked without a weight in the flag, from the one
# rule (noTagTypeAsk), in the database pick and in the copy. The raw threshold
# is gone from both, and so is the second read of the pad.
body = body_of(PICK, "dbPickTypeAsk")
assert squeeze("noTagTypeAsk(spoolTypesActive(), noTagMode(), spoolExtrasBareReading(), in->spool_type_g)") in body, \
    f"src/{PICK}: dbPickTypeAsk() does not decide over noTagTypeAsk(spoolTypesActive(), noTagMode(), ..., in->spool_type_g)"
assert "!in" in body and body.find("!in") < body.find("noTagTypeAsk("), \
    f"src/{PICK}: dbPickTypeAsk() lost its guard for a missing input"
assert "NEW_SPOOL_TARE_MIN_G" not in body and body.count(READING) == 1, \
    f"src/{PICK}: dbPickTypeAsk() still decides on a threshold of its own"
body = body_of(CONFIRM, "showCopyConfirmPopup")
assert squeeze("noTagTypeAsk(spoolTypesActive(), noTagMode(), spoolExtrasBareReading(), 0)") in body, \
    f"src/{CONFIRM}: showCopyConfirmPopup() does not decide over noTagTypeAsk(spoolTypesActive(), noTagMode(), ..., 0)"
assert "NEW_SPOOL_TARE_MIN_G" not in body and body.count(READING) == 1, \
    f"src/{CONFIRM}: showCopyConfirmPopup() still decides on a threshold of its own"
in_order(body, ["noTagTypeAsk(", "buildCopyCard();", "showSpoolTypeList("],
         f"src/{CONFIRM}: showCopyConfirmPopup()")
only_through_rules(CONFIRM, [
    "spoolStartValues(noTagGross(noTagMode(), spoolExtrasBareReading()),",
    "noTagTypeAsk(spoolTypesActive(), noTagMode(), spoolExtrasBareReading(), 0)",
])

# 4. The fill weight on the card for a new spool. Missing in the flag
# (noTagFillMissing, and only where the card could create or asks for a density,
# so a failed plan still says that), the status line says so and the left
# button reads "Fill weight" and opens the price pad under the fill title; it
# comes before the density. The answer: an emptied field changes nothing, a
# value outside the range is refused with the range and not taken, a valid one
# is kept (rounded) in the input, the label weight and the line are made again,
# the plan is asked again (it keys on the weight), and the line goes back to
# the plan.
body = body_of(POPUP, "fillAskDue")
for needle in ("noTagFillMissing(noTagMode(),s_label_weight)", "planCanCreate(s_plan.state)", "s_plan.no_density"):
    assert squeeze(needle) in body, f"src/{POPUP}: fillAskDue() does not use {needle}"

body = body_of(POPUP, "showPlan")
in_order(body, ["if (fillAskDue())", "showFillAsk(STR_NOTAG_FILL_ASK)", "switch ("], f"src/{POPUP}: showPlan()")
assert "showDensityAsk(STR_SORT_NO_DENSITY)" in body, f"src/{POPUP}: showPlan() lost the density ask"

body = body_of(POPUP, "showFillAsk")
assert "showAsk(hint_id,STR_NOTAG_FILL_BTN)" in body, f"src/{POPUP}: showFillAsk() does not name the left button STR_NOTAG_FILL_BTN"
body = body_of(POPUP, "showDensityAsk")
assert "showAsk(hint_id,STR_DENSITY_BTN)" in body, f"src/{POPUP}: showDensityAsk() does not name the left button STR_DENSITY_BTN"
body = body_of(POPUP, "showAsk")
for needle in ("showStatus(T(hint_id),UI_COL_WARN,true)", "UI_FONT_SMALL", "setOkText(button_id)"):
    assert needle in body, f"src/{POPUP}: showAsk() does not do {needle}"

click = click_handler_of(POPUP, "s_job = TCP_CREATE")
ask = "showPricePadInfo((float)s_in.net_weight_g,STR_NOTAG_FILL_TITLE,STR_NOTAG_FILL_INFO,onFill)"
assert squeeze(ask) in click, f"src/{POPUP}: the left button does not open the price pad as {ask}"
in_order(click, ["if (s_job != TCP_IDLE) return;", "if (fillAskDue())", ask, "s_plan.no_density", "s_job = TCP_CREATE"],
         f"src/{POPUP}: the left button")

body = body_of(POPUP, "onFill")
for needle in ("noTagFillPlausible(", "lroundf(", "s_in.net_weight_g=", "s_label_weight=labelWeight()",
               "refreshMeta()", "showPlan()", "s_plan_stale=true", "STR_NOTAG_FILL_RANGE"):
    assert squeeze(needle) in body, f"src/{POPUP}: onFill() does not use {needle}"
assert re.search(r"\w+<=0(?:\.0f)?\)return;", body), f"src/{POPUP}: onFill() does not leave an emptied field (0) alone"
assert body.find("noTagFillPlausible(") < body.find("s_in.net_weight_g="), \
    f"src/{POPUP}: onFill() sets s_in.net_weight_g before it asks noTagFillPlausible()"
assert body.find("STR_NOTAG_FILL_RANGE") < body.find("s_in.net_weight_g="), \
    f"src/{POPUP}: onFill() must say the range on the way out, before the fill weight is kept"
in_order(body, ["s_in.net_weight_g=", "s_label_weight=labelWeight()", "refreshMeta()", "showPlan()"],
         f"src/{POPUP}: onFill()")
# One place keeps the typed fill weight in the input.
assert len(re.findall(r"s_in\.net_weight_g=", squeeze(source_of(POPUP).code))) == 1, \
    f"src/{POPUP}: s_in.net_weight_g is set in more than one place"
# A database entry the plan finds may bring the weight the tag lacks: the line
# must not go on asking for it.
body = body_of(POPUP, "mergeDbEntry")
in_order(body, ["s_label_weight = labelWeight();", "showPlan();"], f"src/{POPUP}: mergeDbEntry()")

# 5. The same on the copy confirmation. A template without a fill weight in the
# flag (copy_template_initial, whole grams) is asked before the copy is made:
# the line says so in the small warning type, the left button reads "Fill
# weight" and opens the same pad instead of creating, the answer is checked
# like the card's, a valid one is kept and the card is built again, an invalid
# one is refused with the range and changes nothing.
body = body_of(CONFIRM, "copyFillMissing")
assert squeeze("noTagFillMissing(noTagMode(), (int)lroundf(copy_template_initial))") in body, \
    f"src/{CONFIRM}: copyFillMissing() does not ask noTagFillMissing(noTagMode(), whole grams of copy_template_initial)"
body = body_of(CONFIRM, "buildCopyCard")
assert "copyFillMissing()" in body and "STR_NOTAG_FILL_BTN" in body and "s_copy_fill_hint" in body, \
    f"src/{CONFIRM}: buildCopyCard() does not name the button and the line for a missing fill weight"
ask = "showPricePadInfo(copy_template_initial,STR_NOTAG_FILL_TITLE,STR_NOTAG_FILL_INFO,copyFillTyped)"
assert squeeze(ask) in body, f"src/{CONFIRM}: the left button does not open the price pad as {ask}"
in_order(body, ["if (copyFillMissing()) {", ask, "copyCreateRequest("], f"src/{CONFIRM}: the left button")
body = body_of(CONFIRM, "copyFillTyped")
assert re.search(r"\w+<=0(?:\.0f)?\)return;", body), f"src/{CONFIRM}: copyFillTyped() does not leave an emptied field (0) alone"
assert "!scr_copy_confirm" in body, f"src/{CONFIRM}: copyFillTyped() takes a fill weight for a card that was closed"
assert "STR_NOTAG_FILL_RANGE" in body and body.find("noTagFillPlausible(") < body.find("copy_template_initial="), \
    f"src/{CONFIRM}: copyFillTyped() sets copy_template_initial before it asks noTagFillPlausible(), or never names the range"
assert body.find("STR_NOTAG_FILL_RANGE") < body.find("copy_template_initial="), \
    f"src/{CONFIRM}: copyFillTyped() must say the range on the way out, before the fill weight is kept"
assert "lroundf(" in body and "buildCopyCard();" in body[body.find("copy_template_initial="):], \
    f"src/{CONFIRM}: copyFillTyped() does not round the answer or build the card again after keeping it"
body = body_of(CONFIRM, "showCopyConfirmPopup")
assert squeeze("s_copy_fill_hint = STR_NOTAG_FILL_ASK;") in body, \
    f"src/{CONFIRM}: showCopyConfirmPopup() does not start every confirmation with the question, not an earlier range"

# 6. The texts are in the table in all three languages, in the words the plan
# gives (no U+2014: a hyphen). The display fonts have no typographic quotes,
# so the quoted button name is written with the plain ones, as in
# STR_SORT_NO_DENSITY. The range text is held to the defines.
lang_h = (src / "lang.h").read_text(encoding="utf-8")
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
rules_text = (src / "services/no_tag_create_rules.h").read_text(encoding="utf-8")


def define_of(name):
    """The value of the one #define name in no_tag_create_rules.h."""
    found = re.findall(rf"^#define[ \t]+{name}[ \t]+([0-9]+)\b", rules_text, re.M)
    assert len(found) == 1, f"src/services/no_tag_create_rules.h: #define {name} is not defined exactly once"
    return int(found[0])


low, high = define_of("NO_TAG_FILL_MIN_G"), define_of("NO_TAG_FILL_MAX_G")
assert (low, high) == (100, 5000), \
    f"src/services/no_tag_create_rules.h: the range is {low} to {high}, but the range text in lang.cpp is written for 100 to 5000"

for sid, texts in (
    ("STR_NOTAG_FILL_BTN", ("Füllmenge", "Filament", "Filament")),
    ("STR_NOTAG_FILL_TITLE", ("Füllmenge (g)", "Filament weight (g)", "Poids de filament (g)")),
    ("STR_NOTAG_FILL_INFO", ("Filament einer vollen Rolle, ohne Spule", "Filament on a full spool, without the spool", None)),
    ("STR_NOTAG_FILL_ASK", ('Füllmenge unbekannt: mit \\"Füllmenge\\" eingeben', 'Filament weight unknown: enter it with \\"Filament\\"', None)),
    ("STR_NOTAG_FILL_RANGE", ("Füllmenge nur von 100 bis 5000 g", "Filament weight only from 100 to 5000 g", "Poids de filament de 100 à 5000 g seulement")),
):
    assert re.search(rf"\b{sid}\b\s*,", lang_h), f"src/lang.h: {sid} is not in the enum"
    row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*' + sid + r"\b", lang_cpp)
    assert row, f"src/lang.cpp: the row of {sid} is not there"
    for lang_name, want, got in zip(("German", "English", "French"), texts, row.groups()):
        assert got, f"src/lang.cpp: {sid} has no {lang_name} text"
        if want is not None:
            assert got == want, f"src/lang.cpp: {sid} ({lang_name}) reads '{got}', expected '{want}'"
        assert "—" not in got, f"src/lang.cpp: {sid} ({lang_name}) has an em-dash"
    if sid == "STR_NOTAG_FILL_RANGE":
        for got in row.groups():
            assert str(low) in got and str(high) in got, \
                f"src/lang.cpp: {sid} '{got}' does not name the range {low} to {high} of no_tag_create_rules.h"

print("no tag weights: ok")
