"""Read the source tree without a compiler and check when the AMS offer after a lift goes stale (Punkteliste n19, Spec 5.3 U2): the offer is held while the questions of a spool just created without a tag are open (the location picker, "Print a label?"), and its two minutes count from the last answer, as the lift clock of a spool shown by its number already does. Before, the clock ran on behind those questions, and a user who took long over them found the AMS question gone. Both offers: the bay picker (Bambuddy, Spoolman) and FilaMan's question."""
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

PICK = "services/ams_pick.cpp"
ASSIGN = "services/ams_assign.cpp"
LOOP = "app/app_loop.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def code_of(rel):
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8")).code


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    code = code_of(rel)
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


# 1. The bay picker: while the questions are open the note's clock starts over,
# before the age is compared.
def bay_picker():
    tick = body_of(PICK, "amsPickTick")
    before(tick, "if (noTagAfterCreateBusy()) s_pending.noted_ms = millis();",
           "amsPickPendingAgeMs() <= AMS_PICK_MAX_MS", f"src/{PICK}: amsPickTick()")


# 2. FilaMan's offer: the same, through a function of its own module, called in
# the loop before the expiry.
def filaman_offer():
    restart = body_of(ASSIGN, "amsPendingRestartClock")
    assert squeeze("if (s_pending.active) s_pending.noted_ms = millis();") in restart, \
        f"src/{ASSIGN}: amsPendingRestartClock() does not start the clock of a note over"
    loop = body_of(LOOP, "appLoop")
    before(loop, "if (amsHasPending() && noTagAfterCreateBusy()) amsPendingRestartClock();",
           "amsPendingAgeMs() > AMS_PENDING_MAX_MS", f"src/{LOOP}: appLoop()")


check(bay_picker)
check(filaman_offer)
for f in failures:
    print("FAIL", f)
if failures:
    sys.exit(1)
print("AMS offer waits for the questions after a create: ok")
