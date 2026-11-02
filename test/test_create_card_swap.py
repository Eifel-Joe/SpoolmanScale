"""Read the source tree without a compiler and check that the create card goes when another spool is laid on (Punkteliste n04): the card from a tag, from "new from database" and from "enter a sort" keeps the key of the tag that was on the pad when it opened (none for a create without a tag), and when another tag is read it closes the way Cancel does and says why on the status line, nothing created. Asked where the card would be built after the spool type list, and on every pass while it stands. Before, the card was built for the first spool and "Create" would have linked its tag with the weight of the second."""
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


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    code = cc.Source(path.read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def before(body, first, second, where):
    """first is in body and stands before the first appearance of second."""
    a, b = body.find(squeeze(first)), body.find(squeeze(second))
    assert a >= 0, f"{where}: '{first}' is not there"
    assert b >= 0, f"{where}: '{second}' is not there"
    assert a < b, f"{where}: '{first}' stands after '{second}'"


failures = []


def check(fn):
    try:
        fn()
    except AssertionError as e:
        failures.append(str(e))


# 1. What the card holds and how it asks: the key of the tag read now, none in
# a create without a tag, compared by the rule.
def held_key():
    hold = body_of(POPUP, "holdPadTag")
    assert squeeze('snprintf(s_held_key, sizeof(s_held_key), "", noTagMode() ? "" : padKeyNow());') in hold, \
        f"src/{POPUP}: holdPadTag() does not keep the key of the tag read now (none without a tag)"
    key = body_of(POPUP, "padKeyNow")
    assert squeeze("return createPadTagKey(g_tag.tray_uuid, g_tag.uid_str);") in key, \
        f"src/{POPUP}: padKeyNow() does not ask the rule for the key of the tag read now"
    fits = body_of(POPUP, "cardTagFits")
    assert squeeze("return createCardTagFits(s_held_key, padKeyNow());") in fits, \
        f"src/{POPUP}: cardTagFits() does not ask the rule"


# 2. Both ways the card opens keep the key, after the input is taken.
def both_openings():
    tag = body_of(POPUP, "showTagCreatePopup")
    before(tag, "tagCreateInputFromTag(&s_in)", "holdPadTag();", f"src/{POPUP}: showTagCreatePopup()")
    before(tag, "holdPadTag();", "tagTypeAsk()", f"src/{POPUP}: showTagCreatePopup()")
    picked = body_of(POPUP, "showTagCreatePopupFor")
    before(picked, "s_in = in;", "holdPadTag();", f"src/{POPUP}: showTagCreatePopupFor()")
    before(picked, "holdPadTag();", "buildCard();", f"src/{POPUP}: showTagCreatePopupFor()")


# 3. The way out: closed as Cancel closes it, the reason on the status line.
def way_out():
    out = body_of(POPUP, "closeForOtherSpool")
    before(out, "closeTagCreatePopup();", "statusMessageShow(T(STR_CREATE_OTHER_SPOOL), UI_COL_WARN);",
           f"src/{POPUP}: closeForOtherSpool()")


# 4. Asked after the spool type list, before the card is built.
def after_type_list():
    go_on = body_of(POPUP, "tagTypeGoOn")
    before(go_on, "if (!cardTagFits()) { closeForOtherSpool(", "buildCard();", f"src/{POPUP}: tagTypeGoOn()")


# 5. Asked on every pass while the card stands, before a create can run.
def while_standing():
    tick = body_of(POPUP, "tagCreatePopupTick")
    before(tick, "if (s_scr && !cardTagFits()) { closeForOtherSpool(", "runCreate();",
           f"src/{POPUP}: tagCreatePopupTick()")


for fn in (held_key, both_openings, way_out, after_type_list, while_standing):
    check(fn)
for f in failures:
    print("FAIL", f)
if failures:
    sys.exit(1)
print("create card goes when another spool is laid on: ok")
