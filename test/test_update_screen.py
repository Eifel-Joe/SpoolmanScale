"""Read the source tree without a compiler and check that the answer of an update check asked for on the GitHub OTA screen reaches that screen while it is open. The switch "Auto check" brings the daily check forward by five seconds (updateCheckScheduleIn), but the handler builds the screen again at once, before the check has run, and the answer used to set only the badges: the open screen showed nothing until it was opened again (seen on the 7B, 25.10.2026). Now the switch says "Checking..." (or "no WiFi") on the screen it builds, the check remembers that the screen asked, and updateCheckTick() hands its answer to otaGithubShowCheckAnswer(), which builds the screen again from what the check stored and only while it is shown. A daily check that lands by chance while the screen is open still leaves a manual result alone: only an answer the screen asked for clears the stored update there."""
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
# once the screen is gone, builds the screen again from what the check stored
# (a found update then reads and installs as after "Check") and says the two
# answers that leave nothing to install with the texts the manual check uses.
header = load(OTA_HEADER)
if header is not None and not re.search(r"\b" + ANSWER + r"\s*\(", header.code):
    errors.append(f"src/{OTA_HEADER}: {ANSWER}() is not declared")
screen = load(OTA_SCREEN)
if screen is not None:
    body = function_body(screen, OTA_SCREEN, ANSWER)
    if body is not None:
        guard = re.search(r"\bif\s*\(\s*!\s*otaGithubScreenVisible\(\)\s*\)\s*return\s*;", body)
        build = body.find("buildOtaGithubScreen()")
        if guard is None or build < 0 or guard.start() > build:
            errors.append(f"src/{OTA_SCREEN}: {ANSWER}() must return while the screen is not shown, "
                          "before it builds the screen again")
        for text in ("STR_GH_OTA_UP_TO_DATE", "STR_GH_OTA_TOO_BIG"):
            if text not in body:
                errors.append(f"src/{OTA_SCREEN}: {ANSWER}() does not say {text}")

    # The switch: switched on, it brings the check forward and, on the screen
    # it has just built, says that the check is on its way.
    build_body = function_body(screen, OTA_SCREEN, "buildOtaGithubScreen")
    if build_body is not None:
        toggle = build_body.find("g_upd_autocheck = !g_upd_autocheck")
        handler = None
        if toggle >= 0:
            opening = build_body.rfind("[](lv_event_t", 0, toggle)
            handler = block_after(build_body, opening) if opening >= 0 else None
        if handler is None:
            errors.append(f"src/{OTA_SCREEN}: the click handler of the switch \"Auto check\" is not there")
        else:
            rebuilt = handler.rfind("buildOtaGithubScreen()")
            if "updateCheckScheduleIn(" not in handler:
                errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" does not bring the check forward")
            if rebuilt < 0:
                errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" does not build the screen again")
            else:
                after = handler[rebuilt:]
                if not re.search(r"\blbl_gh_status\b", after) or "STR_GH_OTA_CHECKING" not in after:
                    errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" does not say STR_GH_OTA_CHECKING "
                                  "on the screen it has built")
                if "STR_GH_OTA_NO_WIFI" not in after:
                    errors.append(f"src/{OTA_SCREEN}: the switch \"Auto check\" says \"checking\" without WiFi, "
                                  "where the check cannot start")

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

# 3. Verdict.
if errors:
    print("update screen: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("update screen: ok")
