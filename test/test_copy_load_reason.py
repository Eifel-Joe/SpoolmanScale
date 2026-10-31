"""Read the source tree without a compiler and check that a template the copy card cannot read says why: no connection, a spool that is gone, or an error the server answered with, the way the other requests of the scale tell them apart.

The card read the template with copyBbsLoad(), which only said yes or no, and
put up "No connection to the server" / "The server did not answer" for every
failure, a 404 or a 500 as well, without telling serverReachNote(), so the
header stayed green while the popup blamed the network (Punkteliste n42,
31.10.2026). The two row reads before a card (the copy list and the link
list) said the same for any answer that was neither a spool, a 404 nor a
network failure. Now the read hands its HTTP code back; a code that never
reached the server goes to serverReachNote(), whose popup and red header the
loop puts up; a 404 says the spool is gone; anything else says the server
answered with an error, the code being in the log."""
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


NEW_STRINGS = ("STR_SERVER_ERR_TITLE", "STR_SERVER_ERR_TEXT", "STR_COPY_TPL_GONE_TITLE", "STR_COPY_TPL_GONE_TEXT")

# 1. The read of the template hands its HTTP code back.
header = read("services/copy_bbs.h")
if header is not None and not re.search(r"\bint\s+copyBbsLoad\(\s*int\b", code_of(header)):
    errors.append("src/services/copy_bbs.h: copyBbsLoad() does not return the HTTP code (int)")

# 2. The card: each cause its own answer, and the network's goes through
# serverReachNote() like everywhere else. The answer lives in a function of
# its own, copyTemplateUnread(), which the card calls with the code of a read
# that failed.
card = read("ui/copy_confirm_popup.cpp")
if card is not None:
    code = code_of(card)
    shown = function_body(code, "void showCopyConfirmPopup(")
    unread = function_body(code, "static void copyTemplateUnread(")
    if shown is None:
        errors.append("src/ui/copy_confirm_popup.cpp: showCopyConfirmPopup() is not there")
    elif unread is None:
        errors.append("src/ui/copy_confirm_popup.cpp: copyTemplateUnread(), the answer to a failed read, is not there")
    elif "copyTemplateUnread(" not in shown:
        errors.append("src/ui/copy_confirm_popup.cpp: showCopyConfirmPopup() does not call copyTemplateUnread()")
    if shown is not None and "STR_SERVER_DOWN_TITLE" in shown:
        errors.append("src/ui/copy_confirm_popup.cpp: showCopyConfirmPopup() still puts up "
                      "\"No connection to the server\" itself")
    if unread is not None:
        body = unread
        for needle, why in (
            ("serverReachNote(", "a template that never reached the server is not told to serverReachNote()"),
            ("serverReachIsNetworkFailure(", "a network failure is not told apart"),
            ("404", "a spool that is gone (404) is not told apart"),
            ("STR_COPY_TPL_GONE_TITLE", "a spool that is gone does not say so"),
            ("STR_SERVER_ERR_TITLE", "an error the server answered with does not say so"),
        ):
            if needle not in body:
                errors.append(f"src/ui/copy_confirm_popup.cpp: copyTemplateUnread(): {why}")
        if "STR_SERVER_DOWN_TITLE" in body:
            errors.append("src/ui/copy_confirm_popup.cpp: copyTemplateUnread() puts up "
                          "\"No connection to the server\" itself instead of serverReachNote()")

# 3. The two row reads before a card: an answer that is not a spool says the
# server answered with an error, not that it did not answer.
for rel, signature in (("ui/copy_flow.cpp", "static void copyRowRefresh("),
                       ("ui/spool_flow.cpp", "static LinkRowRefresh linkRefreshRow(")):
    text = read(rel)
    if text is None:
        continue
    body = function_body(code_of(text), signature)
    if body is None:
        errors.append(f"src/{rel}: {signature}) is not there")
        continue
    if "STR_SERVER_DOWN_TITLE" in body:
        errors.append(f"src/{rel}: {signature}) calls an answer that is not a spool \"No connection to the server\"")
    if "STR_SERVER_ERR_TITLE" not in body:
        errors.append(f"src/{rel}: {signature}) does not say that the server answered with an error")

# 4. The texts, in the table and the enum.
lang_h = read("lang.h")
lang_cpp = read("lang.cpp")
for name in NEW_STRINGS:
    if lang_h is not None and not re.search(r"^\s*" + name + r",", lang_h, re.M):
        errors.append(f"src/lang.h: {name} is missing")
    if lang_cpp is not None and f"// {name}" not in lang_cpp:
        errors.append(f"src/lang.cpp: the row of {name} is missing")

if errors:
    print("copy load reason: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("copy load reason: ok")
