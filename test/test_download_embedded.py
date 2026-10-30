"""Read the web sources without a compiler and check that every download of the scale's pages also works when the pages sit in a sandboxed frame. Bambuddy shows an external page in an iframe whose sandbox lacks allow-downloads, and a browser drops every download that starts inside such a frame, with no file, no entry in the download list and no error: the scale's log said "settings backup downloaded" three times and nothing arrived (acceptance of v0.8.6-beta.8). The frame does grant allow-popups and allow-popups-to-escape-sandbox, so a window opened by the click is free of the sandbox and may download.

The checks: one helper in /app.js decides "embedded" inside a try, opens its window with window.open() as the first thing it does (a popup needs the user's click, so before any fetch or await), and is the only place that sets a download name or clicks a download anchor; the three download sites (settings backup, screenshots, logs) go through it; the backup opens the window before it fetches and closes it on every error; "save all" opens one window for all pictures (a window consumes the click that opened it); an object URL is made in the window that uses it (a URL made in the frame is partitioned away from the popup); the message for a blocked window exists in German, English and French and reaches every page.

Several files for one window (round 1 of the review): the popup has no click of its own, so Chromium allows the first scripted download and prompts or blocks the rest. With more than one file the helper does not click by script; it writes one real download link per file into the window, labelled in the page's language, so each user click carries its own activation, and the window stays open until the user closes it. One file keeps the automatic click and the automatic close. A window the user closed before the data arrived says so with the same sentence as a blocked one."""
import re

import klipper_src as ks
import check_web_js as cw

WEB = ks.src / "web"
SID = "STR_W_DL_EMBEDDED"
SID_SAVE = "STR_W_DL_SAVE"

ks.assert_ci_step("test_download_embedded.py")

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


def js_of(rel):
    """The JavaScript a source file emits, all its buffers one after the other
    (comments out, C++ escapes resolved), as scripts/check_web_js.py replays it."""
    return cw.reconstruct(str(ks.src / rel))


def emitted(rel):
    return "\n".join(js_of(rel).values())


def js_close(text, i):
    """Index of the brace that closes the one at text[i], string literals skipped."""
    depth, j = 0, i
    while j < len(text):
        ch = text[j]
        if ch in "'\"":
            j += 1
            while j < len(text) and text[j] != ch:
                j += 2 if text[j] == "\\" else 1
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return -1


def js_functions(text):
    """{name: (start of the declaration, end of the body, body)} of the named functions."""
    out = {}
    for m in re.finditer(r"\bfunction\s+(\w+)\s*\(", text):
        i = text.index("{", m.end())
        j = js_close(text, i)
        if j > 0:
            out[m.group(1)] = (m.start(), j + 1, text[i + 1:j])
    return out


# ---------------------------------------------------------------- /app.js
appjs = js_of("web/web_static.cpp").get("APP_JS", "")
funcs = js_functions(appjs)
helpers = {n: f for n, f in funcs.items() if n.startswith("download")}
start = helpers.get("downloadStart")
finish = helpers.get("downloadFinish")
abort = helpers.get("downloadAbort")

need(appjs, "src/web/web_static.cpp: no APP_JS literal found")
need(start and finish and abort,
     "src/web/web_static.cpp: /app.js lacks downloadStart(), downloadFinish() or downloadAbort() - the one helper "
     "every download goes through")
helper_text = "\n".join(f[2] for f in helpers.values())
if abort:
    need(".close(" in abort[2],
         "src/web/web_static.cpp: downloadAbort() does not close the window")

# "Embedded" is decided inside a try: reading window.top of a frame from another
# origin may throw, and then the page must still be able to download.
tries = re.findall(r"try\s*\{([^{}]*)\}\s*catch", helper_text)
need(any("window.self" in t and "window.top" in t for t in tries),
     "src/web/web_static.cpp: the download helper does not compare window.self and window.top inside a try")

if start:
    body = start[2]
    at = body.find("window.open(")
    need(at >= 0, "src/web/web_static.cpp: downloadStart() never calls window.open()")
    # A popup needs the click that is still being handled: nothing asynchronous
    # may stand before the call, nor may it sit in a callback.
    before = body[:at] if at >= 0 else body
    need(not re.search(r"\bfunction\b|=>|\.then\(|setTimeout\(|\bawait\b|\bfetch\(", before),
         "src/web/web_static.cpp: downloadStart() calls window.open() after something asynchronous or inside a callback, "
         "and the browser would no longer count it as the user's click")
    need("flash(" in helper_text and "WS.dl" in helper_text and "location.host" in helper_text,
         "src/web/web_static.cpp: the download helper does not say through flash(), with WS.dl and location.host, "
         "that the window could not be opened")
    need("return null" in body,
         "src/web/web_static.cpp: downloadStart() does not answer null when the window was refused")
    # More than one file: the handle says so (list mode) and the window is not
    # given the minute after which it closes itself; the user may still be clicking.
    need(re.search(r"function\s+downloadStart\s*\(\s*\w+\s*,\s*\w+\s*\)", appjs) is not None
         and re.search(r"\blist\s*:", body) is not None,
         "src/web/web_static.cpp: downloadStart(id, n) does not take the number of files and note a list mode in the handle")
    need(re.search(r"if\s*\(\s*!\s*h\.list\s*\)\s*h\.t\s*=\s*setTimeout\(", body) is not None,
         "src/web/web_static.cpp: downloadStart() arms the self-closing timer for a window that holds a list of links")

if finish:
    body = finish[2]
    # An object URL made in the frame belongs to the frame's storage partition
    # and cannot be read from the popup; the window that saves makes its own.
    need(re.search(r"\w+\.URL\.createObjectURL\(", body) is not None,
         "src/web/web_static.cpp: downloadFinish() does not make the object URL through the window that saves "
         "(win.URL.createObjectURL)")
    need(re.search(r"(?<![\w.])URL\.createObjectURL\(", body) is None,
         "src/web/web_static.cpp: downloadFinish() makes an object URL in the frame's own window; the popup cannot read it")
    need(re.search(r"\.download\s*=", body) and ".click()" in body,
         "src/web/web_static.cpp: downloadFinish() does not set the name and click the anchor")
    # A window the user closed: the same sentence as for a blocked one, before
    # anything is made in it.
    closed_at, made_at = body.find(".closed"), body.find("createObjectURL(")
    need(0 <= closed_at < made_at and "downloadNo(" in body[closed_at:made_at],
         "src/web/web_static.cpp: downloadFinish() does not check win.closed (and say downloadNo) before it uses the window")
    # Several files: a real link per file, labelled, no script click; the click
    # stays in the other branch. The window then stays: nothing re-arms the close timer.
    # (js_close skips string literals: the label holds a '{n}' of its own.)
    branch = re.search(r"if\s*\(\s*h\.list\s*\)\s*\{", body)
    first = second = ""
    if branch:
        end = js_close(body, branch.end() - 1)
        first = body[branch.end():end]
        other = re.match(r"\s*else\s*\{", body[end + 1:])
        if other:
            start_at = end + 1 + other.end() - 1
            second = body[start_at + 1:js_close(body, start_at)]
    need(branch and ".click()" not in first and ".click()" in second,
         "src/web/web_static.cpp: downloadFinish() clicks by script when it holds several files; only the single file may be clicked")
    need("WS.dls" in body and "textContent" in body,
         "src/web/web_static.cpp: downloadFinish() does not label the links of a list with WS.dls")
    early = re.search(r"if\s*\(\s*h\.list\s*\)\s*return\s*;", body)
    need(early is not None and body.find("setTimeout(") > early.start(),
         "src/web/web_static.cpp: downloadFinish() arms the self-closing timer for a window that holds a list of links")

# The helper is the only place that sets a download name or clicks an anchor.
rest = appjs
for name, (a, b, body) in sorted(helpers.items(), key=lambda kv: -kv[1][0]):
    rest = rest[:a] + rest[b:]
need(not re.search(r"\bdownload\s*=|\.click\(\)|createObjectURL", rest),
     "src/web/web_static.cpp: /app.js sets a download or clicks an anchor outside the download helper")

# ------------------------------------------------------ the whole web tree
for path in sorted(WEB.rglob("*.cpp")):
    rel = path.relative_to(ks.src).as_posix()
    if rel == "web/web_static.cpp":
        continue
    text = emitted(rel)
    for pat, what in ((r"\bdownload\s*=", "a download name (download=)"),
                      (r"setAttribute\(\s*['\"]download", "a download name (setAttribute)"),
                      (r"\.click\(\)", "an anchor clicked by script")):
        need(not re.search(pat, text),
             f"src/{rel}: still has {what}; a download goes through downloadStart() and downloadFinish()")
    if rel != "web/web_screenshot.cpp":
        need("createObjectURL" not in text,
             f"src/{rel}: makes an object URL; a download hands its Blob to downloadFinish()")

# --------------------------------------------------- the three download sites
SITES = ("web/web_backup.cpp", "web/web_screenshot.cpp", "web/pages/page_logs.cpp")
for rel in SITES:
    text = emitted(rel)
    for name in ("downloadStart", "downloadFinish"):
        need(re.search(rf"\b{name}\(", text), f"src/{rel}: never calls {name}()")
    # The window names the element its message goes to; it has to be there.
    # (The screenshots sit on the logs page and share its elements.)
    page = text
    if rel != "web/web_backup.cpp":
        page = emitted("web/pages/page_logs.cpp") + emitted("web/web_screenshot.cpp")
    for m in re.finditer(r"\bdownloadStart\(([^)]*)\)", text):
        arg = m.group(1).split(",")[0].strip().strip("'\"")
        need(re.fullmatch(r"\w[\w-]*", arg) and re.search(rf"id=['\"]{re.escape(arg)}['\"]", page),
             f"src/{rel}: downloadStart({m.group(1)}) names no element of the page for its message")

# Settings backup: the window first, then the fetch; the sentence of a shut
# gate is still not saved as the backup, and the window goes on every error.
bk = emitted("web/web_backup.cpp")
a = bk.find("$('bk-dl').addEventListener")
b = bk.find("$('bk-up')", a)
need(a >= 0 and b > a, "src/web/web_backup.cpp: the click handler of the download button is not there")
handler = bk[a:b] if a >= 0 and b > a else ""
opened = handler.find("downloadStart(")
need(opened >= 0 and handler.find("fetch(") > opened and "await" not in handler[:max(opened, 0)],
     "src/web/web_backup.cpp: the download button's handler does not open the window (downloadStart) before its fetch")
need(handler.find("r.ok") >= 0 and 0 <= handler.find("r.ok") < handler.find("downloadFinish(") and "r.blob()" in handler,
     "src/web/web_backup.cpp: the backup is no longer fetched, checked with r.ok and handed to downloadFinish() as a blob")
need("downloadAbort(" in handler.partition("r.text()")[2].partition(".catch(")[0],
     "src/web/web_backup.cpp: the refusal branch (r.text()) does not close the window with downloadAbort()")
need("downloadAbort(" in handler.partition(".catch(")[2],
     "src/web/web_backup.cpp: the .catch of the download does not close the window with downloadAbort()")

# Screenshots: the picture's Blob, not the object URL that the <img> uses; and
# "save all" opens one window, because a window consumes the click that opened
# it and a second one would be blocked.
sh = emitted("web/web_screenshot.cpp")
shf = js_functions(sh)
for fn in ("shSave", "shSaveAll"):
    need(fn in shf, f"src/web/web_screenshot.cpp: {fn}() is not there")
if "shSave" in shf:
    calls = ks.calls(shf["shSave"][2], "downloadFinish")
    need(calls and all(len(c) == 3 and ".blob" in c[1] for _, c in calls),
         "src/web/web_screenshot.cpp: shSave() does not hand the picture's Blob (.blob) to downloadFinish()")
if "shSaveAll" in shf:
    body = shf["shSaveAll"][2]
    first = body.find("downloadStart(")
    loop = min([i for i in (body.find("forEach("), body.find("setTimeout(")) if i >= 0] or [len(body)])
    need(0 <= first < loop,
         "src/web/web_screenshot.cpp: shSaveAll() does not open its one window before it starts the pictures")
    args = [c for _, c in ks.calls(body, "downloadStart")]
    need(args and all(len(c) == 2 and "shIds.length" in c[1] for c in args),
         "src/web/web_screenshot.cpp: shSaveAll() does not tell downloadStart() how many pictures follow (shIds.length)")

# Logs: a button and a handler instead of a link; both stores keep their download.
lg = emitted("web/pages/page_logs.cpp")
need(len(re.findall(r"data-dl=", lg)) == 2 and "src=int&dl=1" in lg and "dl=1&file=" in lg,
     "src/web/pages/page_logs.cpp: the ring in flash and the files on the card do not both have a download button (data-dl)")
need(re.search(r"\[data-dl\]", lg) is not None,
     "src/web/pages/page_logs.cpp: the download buttons (data-dl) get no click handler")

# ----------------------------------------------------------------- the text
rows = ks.lang_rows()
if not ks.enum_has(SID):
    problems.append(f"src/lang.h: {SID} is not in the string enum")
elif SID not in rows:
    problems.append(f"src/lang.cpp: {SID} has no row")
else:
    try:
        ks.assert_row_complete(rows, SID, "src/lang.cpp")
    except AssertionError as e:
        problems.append(str(e))
    for lang, cell in zip(("German", "English", "French"), rows[SID]):
        need(cell.count("{a}") == 1 and "http://{a}" in cell,
             f"src/lang.cpp: the {lang} text of {SID} does not end in the address, http://{{a}}, once")

# The label of a link in the list: German "Speichern: {n}", the file name where {n} stands.
if not ks.enum_has(SID_SAVE):
    problems.append(f"src/lang.h: {SID_SAVE} is not in the string enum")
elif SID_SAVE not in rows:
    problems.append(f"src/lang.cpp: {SID_SAVE} has no row")
else:
    try:
        ks.assert_row_complete(rows, SID_SAVE, "src/lang.cpp")
    except AssertionError as e:
        problems.append(str(e))
    for lang, cell in zip(("German", "English", "French"), rows[SID_SAVE]):
        need(cell.count("{n}") == 1,
             f"src/lang.cpp: the {lang} text of {SID_SAVE} does not hold the file name, {{n}}, once")

# Every page gets them, next to the two words of ask(): app.js is cached across languages.
assets = ks.body_of("web/web_shell.cpp", "webShellAssets", strings=True)
need("WS.dl=" in assets and f"jsStr(T({SID}))" in assets,
     f"src/web/web_shell.cpp: webShellAssets() does not assign WS.dl from {SID} on every page")
need("WS.dls=" in assets and f"jsStr(T({SID_SAVE}))" in assets,
     f"src/web/web_shell.cpp: webShellAssets() does not assign WS.dls from {SID_SAVE} on every page")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_download_embedded: ok")
