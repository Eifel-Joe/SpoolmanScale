"""Read the source tree without a compiler and check that the scale's own GitHub requests wait for the background requests: "Check" and "Install" on the GitHub screen and "Install" on the firmware page do not start while the daily check, a web job or the inventory list runs.

The floor of 50 kB is for one such request beside the rest of the firmware
(Spec 5.3 T, "Speicher"), and the background requests already keep apart from
each other and from the inventory list (update_check.cpp maybeStartCheck(),
web_jobs.cpp webJobStart()). The scale's own requests did not: "Check" waited
for the daily check and the web jobs but not for the inventory list, and both
"Install" buttons waited for nothing of it, so a check or a download could run
beside the list with two stacks and two connections (Punkteliste n41,
31.10.2026). Spec 8 T wants the low point of every check and every download at
26 kB or more. One rule says when such a request has to wait, and all three
ask it."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

errors = []


def read(rel):
    path = src / rel
    if not path.is_file():
        errors.append(f"src/{rel}: file is missing")
        return None
    return path.read_text(encoding="utf-8", errors="replace")


def code_of(text):
    """The text with comments and string contents blanked."""
    return cc.Source(text).code


def function_body(code, signature):
    """The text between the braces of the function that starts with signature."""
    start = code.find(signature)
    if start < 0:
        return None
    depth, begin = 0, None
    for index in range(start, len(code)):
        char = code[index]
        if char == "{":
            if depth == 0:
                begin = index + 1
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return code[begin:index]
    return None


RULE = "githubRequestMustWait()"

# 1. The rule: declared once, and it names all three things a GitHub request
# of the scale waits for.
header = read("services/update_check.h")
if header is not None and not re.search(r"\bbool\s+githubRequestMustWait\(\s*\)\s*;", code_of(header)):
    errors.append("src/services/update_check.h: does not declare bool githubRequestMustWait()")
unit = read("services/update_check.cpp")
if unit is not None:
    body = function_body(code_of(unit), "bool githubRequestMustWait(")
    if body is None:
        errors.append("src/services/update_check.cpp: githubRequestMustWait() is not defined")
    else:
        for needle, why in (
            (r"\bs_task_running\b|\bupdateCheckBusy\(\)", "the daily check"),
            (r"webJobState\(\)\s*==\s*WJS_RUNNING", "a web job"),
            (r"backendJobState\(\)\s*==\s*BJS_RUNNING", "the inventory list"),
            # Its stack and connection come from the internal heap, and the
            # scale's own requests have no floor (review of 31.10.2026).
            (r"fdbState\(\)\s*==\s*FDB_RUNNING", "the database job of \"Neu aus Datenbank\""),
        ):
            if not re.search(needle, body):
                errors.append(f"src/services/update_check.cpp: githubRequestMustWait() does not wait for {why}")

# 2. "Check" on the scale: the deferred retry in the loop and the check itself.
loop = read("app/app_loop.cpp")
if loop is not None:
    code = code_of(loop)
    at = code.find("if (gh_check_pending)")
    block = function_body(code[at:], "if (gh_check_pending)") if at >= 0 else None
    if block is None:
        errors.append("src/app/app_loop.cpp: the retry of a deferred check (gh_check_pending) is not there")
    elif RULE not in block:
        errors.append(f"src/app/app_loop.cpp: the retry of a deferred check does not ask {RULE}")

screen = read("ui/ota_github.cpp")
if screen is not None:
    code = code_of(screen)
    for signature, what in (("void doGithubOtaCheck(", "the check"),
                            ("void doGithubOtaFlash(", "the download")):
        body = function_body(code, signature)
        if body is None:
            errors.append(f"src/ui/ota_github.cpp: {signature}) is not there")
            continue
        if RULE not in body:
            errors.append(f"src/ui/ota_github.cpp: {what} on the scale ({signature})) does not ask {RULE}")
    # A download that waited too long gives up and says so, as the check does.
    # It installs the version that was confirmed, not one a check rewrote
    # meanwhile, and only while its screen is still shown (review of
    # 31.10.2026).
    flash = function_body(code, "void doGithubOtaFlash(")
    if flash is not None:
        if "GH_CHECK_WAIT_MS" not in flash:
            errors.append("src/ui/ota_github.cpp: doGithubOtaFlash() waits without a limit (GH_CHECK_WAIT_MS)")
        if "s_flash_wait_tag" not in flash:
            errors.append("src/ui/ota_github.cpp: a waiting install does not keep the version it was asked for "
                          "(s_flash_wait_tag)")
        if "otaGithubScreenVisible()" not in flash:
            errors.append("src/ui/ota_github.cpp: a waiting install goes on after its screen was left")

# The wait covers the inventory list of a lookup at its longest: two attempts
# of 20 s and the archive pass of 8 s (lookup_scan.cpp).
deferred = read("app/deferred_actions.h")
if deferred is not None:
    m = re.search(r"#define\s+GH_CHECK_WAIT_MS\s+(\d+)", deferred)
    if not m or int(m.group(1)) < 48000:
        errors.append("src/app/deferred_actions.h: GH_CHECK_WAIT_MS is shorter than the longest inventory list (48 s)")

# 3. "Install" on the firmware page, run from the loop.
page = read("web/pages/page_firmware.cpp")
if page is not None:
    body = function_body(code_of(page), "void otaWebGithubTick(")
    if body is None:
        errors.append("src/web/pages/page_firmware.cpp: otaWebGithubTick() is not there")
    else:
        if RULE not in body:
            errors.append(f"src/web/pages/page_firmware.cpp: the install from the web UI does not ask {RULE}")
        if "GH_CHECK_WAIT_MS" not in body:
            errors.append("src/web/pages/page_firmware.cpp: the install from the web UI waits without a limit "
                          "(GH_CHECK_WAIT_MS)")
        if "s_web_install_gave_up" not in body:
            errors.append("src/web/pages/page_firmware.cpp: an install from the web UI that gave up waiting "
                          "is not remembered for the page (s_web_install_gave_up)")
    # The page hears it: /api/ota/progress says so, and the box closes with
    # the "busy" text instead of five minutes of "keep the page open".
    full = cc.Source(page).code_str
    at = full.find('srv.on("/api/ota/progress"')
    progress = full[at:full.find("});", at)] if at >= 0 else ""
    if "s_web_install_gave_up" not in progress or "failed" not in progress:
        errors.append("src/web/pages/page_firmware.cpp: /api/ota/progress does not report an install that gave up")
    if not re.search(r"d&&d\.failed", full):
        errors.append("src/web/pages/page_firmware.cpp: the install box does not close on an install that gave up")

if errors:
    print("github wait: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("github wait: ok")
