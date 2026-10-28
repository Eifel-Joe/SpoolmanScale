"""Read the source tree without a compiler and check the location after a spool created without a tag (X, V39): the ending arms the location picker for the new spool when it is on the screen, whatever "location on removal" says and with or without a load cell; the loop opens it a pass later, the same list as "More info" -> "Location", marked as asked so that the removal does not ask the spool again; the question for a label is armed only once the picker has ended by itself (a row, "no location", the X, Cancel, its countdown, or not shown at all), after its own way back to the main screen, and not when a navigation took it down; the removal's own offer for a tag spool stays as it was."""
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

CORE = "ui/no_tag_create.cpp"
HEADER = "ui/no_tag_create.h"
INFO = "ui/more_info_screen.cpp"
INFO_H = "ui/more_info_screen.h"
LOOP = "app/app_loop.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def source_of(rel):
    """One file under src/ as the ratchet reads it."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def functions_of(rel):
    """{name: squeezed body} of every function defined once in the file."""
    code = source_of(rel).code
    out = {}
    for name, start, end in cc.find_functions(code):
        out.setdefault(name, []).append(squeeze(code[start + 1:end]))
    return out


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    bodies = functions_of(rel).get(name, [])
    assert len(bodies) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return bodies[0]


def without_logs(code):
    """The squeezed code with the log calls taken out."""
    return re.sub(r'logSDf?\([^;]*\);', "", code)


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


# 1. The ending arms the picker, not the question for a label. Only when the
# new spool is the one on the screen after all (idSpoolAdopt() leaves the
# screen to a tag on the pad, and the load can fail): then neither question
# comes. Nothing else decides it: not "location on removal", not the load
# cell, not the printer. The picker itself opens from the loop a pass later
# (the choice and the lists were released in this pass).
where = f"src/{CORE}: noTagCreateFinish()"
body = body_of(CORE, "noTagCreateFinish")
arm = "s_loc_for = (sm_found && sm_id == new_spool_id) ? new_spool_id : 0;"
once(body, arm, where)
at = body.find(squeeze(arm))
assert depth_at(body, at) == 0 and body[at - 1] in ";}{", f"{where}: the location picker is armed conditionally"
once(body, "s_loc_wait = s_loc_for != 0;", where)
in_order(body, ["noTagModeSet(false);", arm, "s_loc_wait = s_loc_for != 0;"], where)
for banned in ("s_ask_for", "s_ask_wait", "requestLocationPicker", "tapLocationButton", "labelPrinterReadiness",
               "g_auto_loc_popup", "g_scale_fitted"):
    assert banned not in body, f"{where}: '{banned}' is in the ending (the picker opens from the loop, the label question follows it)"

# 2. The loop: the picker before the question for a label. Right after the
# card's own part (a card already up is left alone), and while it is armed
# the question for a label waits.
where = f"src/{CORE}: noTagLabelAskTick()"
body = body_of(CORE, "noTagLabelAskTick")
_, _, end = block_of(body, "if (s_card) {", where)
rest = body[end:]
step = squeeze("if (s_loc_for) { askLocation(); return; }")
assert rest.startswith(step), \
    f"{where}: right after the card's part the location picker must come, and hold the question for a label"
in_order(rest, [step, "if (!s_ask_for) return;"], where)
once(body, "askLocation();", where)

# A pass after the ending, for the spool that is still the one on the screen,
# marked as asked for the removal (g_loc_popup_shown_for_id, as every other
# offer does before it asks), then the same list as "More info" -> "Location"
# with its ways out to the main screen; requestLocationPickerThen() reports
# its end to locationPickerEnded().
where = f"src/{CORE}: askLocation()"
body = body_of(CORE, "askLocation")
in_order(body, ["if (s_loc_wait) { s_loc_wait = false; return; }", "const int id = s_loc_for;", "s_loc_for = 0;",
                "if (!shownStill(id)) {", "g_loc_popup_shown_for_id = id;",
                "requestLocationPickerThen(id, locationPickerEnded);"], where)
gone, _, _ = block_of(body, "if (!shownStill(id)) {", where)
assert without_logs(gone) == "return;" and "logSD" in gone, \
    f"{where}: a spool no longer on the screen gets no picker, says so in the log, and nothing else"
for banned in ("g_auto_loc_popup", "g_scale_fitted", "wifi_ok", "labelPrinter", "tapLocationButton(",
               "requestLocationPicker(", "s_ask_for"):
    assert banned not in body, f"{where}: '{banned}' decides the picker (V39: at every create, whatever the setting)"
core = squeeze(source_of(CORE).code)
once(core, "requestLocationPickerThen(", f"src/{CORE}")
assert "requestLocationPicker(" not in core and "tapLocationButton(" not in core, \
    f"src/{CORE}: the picker is asked for in a way that does not report its end"
raw = (src / CORE).read_text(encoding="utf-8")
for inc in ('#include "ui/more_info_screen.h"', '#include "services/location_state.h"'):
    assert inc in raw, f"src/{CORE}: {inc} is missing"

# 3. The hand-over. When the picker has ended by itself, the question for a
# label is armed under its own rules (the spool still the one on the screen,
# a printer ready; the card a pass later, the answer checked again). It is the
# one place the question is armed with a spool: everywhere else it is only
# cleared.
where = f"src/{CORE}: locationPickerEnded()"
body = body_of(CORE, "locationPickerEnded")
assert without_logs(body) == squeeze("s_ask_for = (shownStill(spool_id) && labelPrinterReadiness() == LP_OK) ? spool_id : 0; "
                                     "s_ask_wait = s_ask_for != 0;"), \
    f"{where}: must arm the question for a label for a spool still on the screen with a printer ready, and nothing else"
assert "logSD" in body, f"{where}: the end of the picker is not in the log"
arms = [a for a in re.findall(r"s_ask_for=([^;=][^;]*);", core) if a != "0"]
assert arms == [squeeze("(shownStill(spool_id) && labelPrinterReadiness() == LP_OK) ? spool_id : 0")], \
    f"src/{CORE}: the question for a label is armed elsewhere than in locationPickerEnded(): {arms}"
once(core, "s_ask_wait = s_ask_for != 0;", f"src/{CORE}")

# A navigation forgets a picker that was armed and not yet asked for, as it
# forgets the question for a label (the main screen it was asked on is gone).
where = f"src/{CORE}: closeNoTagLabelAsk()"
body = body_of(CORE, "closeNoTagLabelAsk")
for needle in ("s_loc_for = 0;", "s_loc_wait = false;", "s_ask_for = 0;", "s_ask_wait = false;"):
    assert squeeze(needle) in body, f"{where}: does not do {needle}"

# 4. The picker. requestLocationPickerThen() is the removal's request (true:
# its ways out go to the main screen) with one who waits for its end. Every
# other request, and a navigation that takes the picker down, forget that one.
where = f"src/{INFO}: requestLocationPickerThen()"
body = body_of(INFO, "requestLocationPickerThen")
assert body == squeeze("requestLocationPicker(true); s_loc_ended = LocEnded{ended, spool_id};"), \
    f"{where}: must be the request with ways out to the main screen, and remember who waits for its end"
assert squeeze("s_loc_ended = LocEnded{};") in body_of(INFO, "requestLocationPicker"), \
    f"src/{INFO}: requestLocationPicker() does not forget who waited for the picker before"
assert body_of(INFO, "tapLocationButton").startswith(squeeze("requestLocationPicker(from_popup);")), \
    f"src/{INFO}: tapLocationButton() does not go through requestLocationPicker() first"
assert squeeze("s_loc_ended = LocEnded{};") in body_of(INFO, "hideMoreInfoOverlays"), \
    f"src/{INFO}: hideMoreInfoOverlays() leaves the one who waited for the picker; a navigation would hand over to the label question"
assert body_of(INFO, "locEndedTake") == squeeze("const LocEnded e = s_loc_ended; s_loc_ended = LocEnded{}; return e;"), \
    f"src/{INFO}: locEndedTake() must hand over who waited and forget it"
assert body_of(INFO, "locEndedRun") == squeeze("if (e.fn) e.fn(e.spool_id);"), \
    f"src/{INFO}: locEndedRun() must call the one who waited, with its spool, and only when there is one"
writers = sorted(name for name, bodies in functions_of(INFO).items()
                 if any(re.search(r"\bs_loc_ended=", b) for b in bodies))
assert writers == sorted(["requestLocationPicker", "requestLocationPickerThen", "hideMoreInfoOverlays", "locEndedTake"]), \
    f"src/{INFO}: s_loc_ended is written by {writers}"

# Every way the picker ends by itself hands over, after its own way back to the
# main screen (showMainScreen() reaches hideAllOverlays(), which forgets every
# question armed before it): taken before the picker goes, run after.
where = f"src/{INFO}: handleMoreInfoDeferredActions()"
body = body_of(INFO, "handleMoreInfoDeferredActions")
for head, first in (("if (loc_cancel_pending) {", "loc_cancel_pending = false;"),
                    ("if (loc_patch_pending) {", "loc_patch_pending = false;")):
    inner, _, _ = block_of(body, head, where)
    assert inner.startswith(squeeze(first + "const LocEnded ended = locEndedTake(); closeLocationPicker();")), \
        f"{where} ({head}): who waits is not taken before the picker closes"
    assert inner.endswith(squeeze("if (g_loc_picker_from_popup) showMainScreen(); else showMoreInfoScreen(); "
                                  "locEndedRun(ended);")), \
        f"{where} ({head}): the end is not reported last, after the way back"
    once(inner, "locEndedRun(ended);", f"{where} ({head})")
# The choice still marks the spool as asked, as before.
patch, _, _ = block_of(body, "if (loc_patch_pending) {", where)
once(patch, "g_loc_popup_shown_for_id = sm_id;", where + " (a choice)")

# Not shown at all is an end as well: the spool left the screen (then the
# question for a label does not come either) or it is archived.
where = f"src/{INFO}: showLocationPicker()"
body = body_of(INFO, "showLocationPicker")
inner, _, _ = block_of(body, "if (!sm_found || (sm_archived && !g_loc_picker_by_tap) || sm_id <= 0) {", where)
assert without_logs(inner) == squeeze("locEndedRun(locEndedTake()); return;"), \
    f"{where}: a picker that is not shown does not report its end"
info = squeeze(source_of(INFO).code)
assert info.count("locEndedRun(") == 4 and info.count("locEndedTake()") == 4, \
    f"src/{INFO}: the end is reported in other places than the cancel, the choice and the picker not shown"
assert squeeze("void requestLocationPickerThen(int spool_id, void (*ended)(int spool_id));") in \
    squeeze(source_of(INFO_H).code), f"src/{INFO_H}: requestLocationPickerThen() is not declared"

# 5. The removal's own offer for a tag spool stays as it was: its setting, the
# load cell, the WLAN and "not asked for this spool yet" decide it, it marks
# the spool, and nobody waits for its end.
loop = squeeze(source_of(LOOP).code)
once(loop, "if (g_scale_fitted && g_auto_loc_popup && sm_found && !sm_archived && sm_id > 0 && wifi_ok && "
           "g_loc_popup_shown_for_id != sm_id) {", f"src/{LOOP}")
once(loop, "g_loc_popup_shown_for_id = pending_id; requestLocationPicker(true);", f"src/{LOOP}")
users = sorted(str(p.relative_to(src)).replace("\\", "/") for p in src.rglob("*")
               if p.suffix in (".cpp", ".h") and "requestLocationPickerThen" in p.read_text(encoding="utf-8"))
assert users == sorted([CORE, INFO, INFO_H]), f"src: requestLocationPickerThen() is used in {users}"

# 6. No new text: the picker's own title.
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                r"STR_LOCATION_TITLE\b", lang_cpp)
assert row and row.groups() == ("Lagerort wählen", "Select location", "Choisir l'emplacement"), \
    "src/lang.cpp: STR_LOCATION_TITLE is not 'Lagerort wählen' / 'Select location' / 'Choisir l'emplacement'"

# 7. The step is in the pipeline, right after the one for "into AMS".
steps = re.findall(r"- name: (Test [^\n]*)\n\s+run: ([^\n]*)", (root / ".github/workflows/ci.yml").read_text(encoding="utf-8"))
names = [name for name, _ in steps]
assert "Test no tag location" in names, ".github/workflows/ci.yml: the step 'Test no tag location' is not there"
assert names.index("Test no tag location") == names.index("Test ams info") + 1, \
    ".github/workflows/ci.yml: 'Test no tag location' does not come right after 'Test ams info'"
assert dict(steps)["Test no tag location"] == "python3 test/test_no_tag_location.py", \
    ".github/workflows/ci.yml: the step does not run python3 test/test_no_tag_location.py"

print("no tag location: ok")
