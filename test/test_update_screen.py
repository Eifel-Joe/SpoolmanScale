"""Read the source tree without a compiler and check that the answer of an update check asked for on the GitHub OTA screen reaches that screen while it is open. The switch "Auto check" brings the daily check forward by five seconds (updateCheckScheduleIn), and the answer used to set only the badges: the open screen showed nothing until it was opened again (seen on the 7B, 25.10.2026). The original's new update screen (3969d87) is built once and its handlers no longer build it again: they write the headline with ghShowStatus() and the button with ghSetAction(), and ghShowInitial() takes the screen back to what the check stored. The rule stays: the switch says "Checking..." (or "no WiFi") after it has taken the screen back to its opening state, the check remembers that the screen asked, and updateCheckTick() hands its answer to otaGithubShowCheckAnswer(), which writes only the status line and the button, only while the screen is shown and never by building the screen again (an open downgrade question stays on top), and says "up to date", "too big" or "check failed" where nothing is to install. A daily check that lands by chance while the screen is open still leaves a manual result alone: only an answer the screen asked for clears the stored update there."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

UPDATE_CHECK = "services/update_check.cpp"
OTA_SCREEN = "ui/ota_github.cpp"
OTA_HEADER = "ui/ota_github.h"
ANSWER = "otaGithubShowCheckAnswer"

errors = []


def load(rel):
    path = src / rel
    if not path.is_file():
        errors.append(f"src/{rel}: file is missing")
        return None
    return cc.Source(path.read_text(encoding="utf-8", errors="replace"))


def function_body(source, rel, name):
    """The code between the braces of the one function called name, or None."""
    spans = [(start, end) for found, start, end in cc.find_functions(source.code) if found == name]
    if len(spans) != 1:
        errors.append(f"src/{rel}: {name}() is not defined exactly once")
        return None
    return source.code[spans[0][0] + 1:spans[0][1]]


def block_after(code, offset):
    """The text between the braces of the block that opens after offset."""
    begin = code.find("{", offset)
    if begin < 0:
        return None
    depth = 0
    for index in range(begin, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[begin + 1:index]
    return None


# 1. The screen offers the answer: declared in its header, and it does nothing
# once the screen is gone. The original's new screen (3969d87) is built once
# and its handlers never build it again; they write the headline with
# ghShowStatus() and the button with ghSetAction(). So the answer writes the
# status line the same way, says the two answers that leave nothing to install
# with the texts the manual check uses, and hands a found update to
# ghShowInitial(), which reads what the check stored (a found update then
# installs as after "Check"). A downgrade question that is open sits above
# the screen and stays there, because nothing here builds the screen again.
header = load(OTA_HEADER)
if header is not None and not re.search(r"\b" + ANSWER + r"\s*\(", header.code):
    errors.append(f"src/{OTA_HEADER}: {ANSWER}() is not declared")
screen = load(OTA_SCREEN)
if screen is not None:
    body = function_body(screen, OTA_SCREEN, ANSWER)
    if body is not None:
        guard = re.search(r"\bif\s*\(\s*!\s*otaGithubScreenVisible\(\)\s*\)\s*return\s*;", body)
        first_show = re.search(r"\bghShow\w*\s*\(", body)
        if guard is None or (first_show is not None and guard.start() > first_show.start()):
            errors.append(f"src/{OTA_SCREEN}: {ANSWER}() must return while the screen is not shown, "
                          "before the first ghShow call")
        if re.search(r"\bbuildOtaGithubScreen\s*\(", body):
            errors.append(f"src/{OTA_SCREEN}: {ANSWER}() builds the screen again, "
                          "a waiting dialog would end up under it")
        # A failed check says so (Jörg, 26.10.2026: "Suche fehlgeschlagen"),
        # with the text the web page already has in all three languages.
        for text in ("STR_GH_OTA_UP_TO_DATE", "STR_GH_OTA_TOO_BIG", "STR_W_FW_CHECK_FAIL"):
            if text not in body:
                errors.append(f"src/{OTA_SCREEN}: {ANSWER}() does not say {text}")
        for call in ("ghShowStatus", "ghSetAction", "ghShowInitial"):
            if not re.search(r"\b" + call + r"\s*\(", body):
                errors.append(f"src/{OTA_SCREEN}: {ANSWER}() does not call {call}()")

    # The switch: it takes the screen back to its opening state, brings the
    # check forward and, after that, says that the check is on its way (or
    # that there is no WiFi to start it with).
    toggle = function_body(screen, OTA_SCREEN, "ghOnAutoToggle")
    if toggle is not None:
        if "updateCheckScheduleIn(" not in toggle:
            errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" does not bring the check forward")
        if re.search(r"\bbuildOtaGithubScreen\s*\(", toggle):
            errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" builds the screen again from its handler")
        opening = [m.end() for m in re.finditer(r"\bghShowInitial\s*\(\s*\)", toggle)]
        if not opening:
            errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" does not take the screen back to "
                          "its opening state (ghShowInitial())")
        after = toggle[opening[-1]:] if opening else toggle
        if "STR_GH_OTA_CHECKING" not in after:
            errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" does not say STR_GH_OTA_CHECKING "
                          "after the last ghShowInitial()")
        if "STR_GH_OTA_NO_WIFI" not in after:
            errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" says \"checking\" without WiFi, "
                          "where the check cannot start (no STR_GH_OTA_NO_WIFI after the last ghShowInitial())")
        # And the switch on the screen is that handler.
        build_body = function_body(screen, OTA_SCREEN, "buildOtaGithubScreen")
        if build_body is not None and not re.search(r"\bghOnAutoToggle\b", build_body):
            errors.append(f"src/{OTA_SCREEN}: buildOtaGithubScreen() does not hand ghOnAutoToggle "
                          "to the switch \"Auto check\"")

# 2. The check: updateCheckScheduleIn() remembers that the screen asked, and
# updateCheckTick() takes that back with the answer and hands the answer on
# only then. The up to date answer clears the stored update on the open
# screen only when the screen asked for it.
check = load(UPDATE_CHECK)
if check is not None:
    schedule = function_body(check, UPDATE_CHECK, "updateCheckScheduleIn")
    tick = function_body(check, UPDATE_CHECK, "updateCheckTick")
    flags = [m.group(1) for m in re.finditer(r"\b(\w+)\s*=\s*true\s*;", schedule)] if schedule else []
    asked = [name for name in flags if name != "s_scheduled"]
    if schedule is not None and len(asked) != 1:
        errors.append(f"src/{UPDATE_CHECK}: updateCheckScheduleIn() does not remember in one flag "
                      "that the screen asked for the check")
    if tick is not None and len(asked) == 1:
        name = asked[0]
        copy = re.search(r"\b(?:const\s+)?bool\s+(\w+)\s*=\s*" + name + r"\s*;", tick)
        cleared = re.search(r"\b" + name + r"\s*=\s*false\s*;", tick)
        names = name + (r"|" + copy.group(1) if copy else "")
        call = re.search(r"\bif\s*\(\s*(?:" + names + r")\s*\)\s*\{?\s*" + ANSWER + r"\s*\(", tick)
        if copy is None or cleared is None or copy.start() > cleared.start():
            errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() does not take {name} with the answer "
                          f"(a copy first, then {name} = false)")
        if call is None:
            errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() does not hand the answer to {ANSWER}() "
                          f"under if ({name} or its copy)")
        if not re.search(r"\bif\s*\(\s*!\s*otaGithubScreenVisible\(\)\s*\|\|\s*(?:" + names + r")\s*\)", tick):
            errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() keeps the stored update on the open screen "
                          f"even when the screen asked (if (!otaGithubScreenVisible() || {name}))")
        # All of it belongs to a result: inside if (result != RES_NONE), the
        # flag taken before the switch and the answer handed on after it, once
        # the stored update and the badges are set.
        handled = re.search(r"\bif\s*\(\s*result\s*!=\s*RES_NONE\s*\)", tick)
        block = block_after(tick, handled.end()) if handled else None
        switch = re.search(r"\bswitch\s*\(\s*result\s*\)", block or "")
        if block is None or switch is None:
            errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() has no if (result != RES_NONE) with a switch")
        else:
            switch_body = block_after(block, switch.end())
            switch_end = block.find(switch_body) + len(switch_body) if switch_body else -1
            inner_copy = re.search(r"\b(?:const\s+)?bool\s+\w+\s*=\s*" + name + r"\s*;", block)
            inner_call = re.search(r"\b" + ANSWER + r"\s*\(", block)
            if inner_copy is None or inner_copy.start() > switch.start():
                errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() does not take {name} inside "
                              "if (result != RES_NONE), before the switch")
            if inner_call is None or inner_call.start() < switch_end:
                errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() does not hand the answer on inside "
                              "if (result != RES_NONE), after the switch")
            # Each result says which answer it is; the ones not named stay
            # GH_ANSWER_FAILED, the value the answer starts with.
            if not re.search(r"\bGhCheckAnswer\s+(\w+)\s*=\s*GH_ANSWER_FAILED\s*;", block[:switch.start()]):
                errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() does not start the answer at "
                              "GH_ANSWER_FAILED before the switch")
            cases = {m.group(1): m.end() for m in re.finditer(r"\bcase\s+(RES_\w+)\s*:", switch_body or "")}
            for result, answers in (("RES_AVAILABLE", ("GH_ANSWER_AVAILABLE", "GH_ANSWER_TOO_BIG")),
                                    ("RES_UP_TO_DATE", ("GH_ANSWER_UP_TO_DATE",))):
                if result not in cases:
                    errors.append(f"src/{UPDATE_CHECK}: updateCheckTick() has no case {result}")
                    continue
                later = [offset for offset in cases.values() if offset > cases[result]]
                case_text = switch_body[cases[result]:min(later) if later else len(switch_body)]
                for answer in answers:
                    if not re.search(r"=\s*" + answer + r"\s*;", case_text):
                        errors.append(f"src/{UPDATE_CHECK}: case {result} does not set the answer {answer}")

# 3. Verdict.
if errors:
    print("update screen: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("update screen: ok")
