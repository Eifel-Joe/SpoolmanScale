"""Read the source tree without a compiler and check that the pages of the web interface turn a request down in the page's language (Punkteliste n63).

The logs page, the label page, the time zone of the settings and the screenshots answered a broken or incomplete request with an English sentence ("Missing file param", "bad layout", "unknown zone", "No such screenshot" ...), most of it code of the original. The pages do not send such requests themselves; a link opened by hand, a log file that is gone or a missing SD card show the answer as it is. The printer page answers in the page's language since v0.8.6-beta.19 (test_label_printer_texts.py), and these do the same now: a text of the table, or a JSON error whose value is one (jsonEsc(T(...))).

Looked through as sister paths: the failures of the same routes (5xx, "Open failed", "Failed to store the setting", "Screenshot failed, no memory"), the page that does not apply to a board ("Not found" in web_server.cpp) and the answer while an image is written ("flashing", web_access.cpp) are shown the same way and answer in the page's language too. Left in English on purpose: the routes of FilaMan's device protocol (a token refused, no FilaMan configured, a broken trigger), which a server reads and no person; the JSON flags {"ok":false} that the pages turn into a text of their own; the routes of the acceptance firmware (web_doc.cpp), which never ship."""
import re

import klipper_src as ks

ks.assert_ci_step("test_web_refusal_texts.py")

# Every 4xx and 5xx answer of these files with a body of its own.
WHOLE = ("web/pages/page_logs.cpp", "web/pages/page_label.cpp", "web/pages/page_config.cpp",
         "web/web_screenshot.cpp")
# A flag, not a sentence: the page shows a text of its own for it.
FLAGS = {'"{\\"ok\\":false}"'}

problems = []
rows = ks.lang_rows()


def check_body(rel, status, body_arg):
    """One answer: a text of the table, or a JSON error that carries one."""
    body_arg = ks.collapse(body_arg)
    if body_arg in FLAGS:
        return
    ids = re.findall(r"\bT\((STR_\w+)\)", body_arg)
    plain = re.fullmatch(r"T\(STR_\w+\)", body_arg)
    as_json = re.search(r"jsonEsc\(T\(STR_\w+\)\)", body_arg)
    # T(labelPrintResultString(r)) and the like: a text of the table, picked at run time.
    picked = body_arg.startswith("T(") or "? reason : T(" in body_arg
    if not (plain or as_json or picked):
        problems.append(f"src/{rel}: send({status}, ...) answers with {body_arg}, not a text of the table")
        return
    if as_json and not body_arg.startswith("String(\"{\\\"error\\\":\\\"\")"):
        problems.append(f"src/{rel}: send({status}, ...) wraps its text in something else than {{\"error\":\"...\"}}")
    for sid in ids:
        try:
            ks.assert_row_complete(rows, sid, f"src/{rel}: send({status}, ...)")
        except AssertionError as e:
            problems.append(str(e))


def answers(rel, text):
    """[(status, body)] of every send(4xx|5xx, type, body) in text."""
    out = []
    for _, args in ks.calls(text, "send"):
        if len(args) == 3 and re.fullmatch(r"[45]\d\d", args[0].strip()):
            out.append((args[0].strip(), args[2]))
    return out


for rel in WHOLE:
    for status, body in answers(rel, ks.source_of(rel).code_str):
        check_body(rel, status, body)

# The page that does not apply to this board ("there is no backend page at all
# on Spoolman"): the browser shows its 404 as it is.
rel = "web/web_server.cpp"
code = ks.source_of(rel).code_str
at = code.find("if (pg->applies && !pg->applies())")
if at < 0:
    problems.append(f"src/{rel}: the 404 of a page that does not apply is not found")
else:
    found = answers(rel, code[at:code.find("return;", at)])
    if len(found) != 1:
        problems.append(f"src/{rel}: the 404 of a page that does not apply is not one send(404, ...)")
    for status, body in found:
        check_body(rel, status, body)

# While an image is written every gated route answers 503; a page opened then
# shows the text. FilaMan's device protocol (404, 401) stays as it is.
rel = "web/web_access.cpp"
body = ks.body_of(rel, "webRequire", strings=True)
flashing = [(s, b) for s, b in answers(rel, body) if s == "503"]
if len(flashing) != 1:
    problems.append(f"src/{rel}: webRequire() has not exactly one send(503, ...) for an image being written")
for status, b in flashing:
    check_body(rel, status, b)
# The device protocol: no FilaMan configured (404), a token refused (401 in
# JSON). The PIN of the web access is a person's 401 and in the table already.
protocol = [a for _, a in ks.calls(body, "send")
            if len(a) == 3 and (a[0].strip() == "404" or
                                (a[0].strip() == "401" and "application/json" in a[1]))]
if len(protocol) != 2:
    problems.append(f"src/{rel}: webRequire() has not the two answers of FilaMan's device protocol (404, 401 JSON)")
for a in protocol:
    if "T(" in a[2]:
        problems.append(f"src/{rel}: webRequire() answers FilaMan's device protocol ({a[0].strip()}) "
                        "in the page's language; a server reads it")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_web_refusal_texts: ok")
