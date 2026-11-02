#!/usr/bin/env python3
"""Reconstruct the JavaScript the web pages emit and run `node --check` on it.

A page body is JavaScript inside C++ string literals: two levels of escaping,
and the compiler checks only one. `\'` in a C++ literal is just `'` by the
time it reaches the browser, and one syntax error kills the whole <script>
block, silently. `pio run` cannot see that class of fault. This script can.

It walks every .cpp under src/web, replays each `x += ...` statement into a
buffer per accumulator (unescaping the literals the way the compiler does),
stands in a placeholder for every value the firmware injects at runtime
(`jsStr(...)` becomes "x", anything else becomes 0), then cuts out the
<script> blocks and hands each one to node. The static app.js literal is
checked whole.

Exit code 1 when node rejects a block, 2 when node is missing.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "src", "web")

# C++ escape sequences as the compiler resolves them.
_ESC = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'", "?": "?", "a": "", "b": "", "f": "", "v": ""}


def unescape(body: str) -> str:
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        if i >= len(body):
            break
        c = body[i]
        if c == "x":
            m = re.match(r"[0-9A-Fa-f]{1,2}", body[i + 1 :])
            if m:
                out.append(chr(int(m.group(0), 16)))
                i += 1 + len(m.group(0))
                continue
        if c in _ESC:
            out.append(_ESC[c])
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def strip_comments(src: str) -> str:
    """Remove // and /* */ comments, leaving string literals intact."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == q:
                    break
                j += 1
            out.append(src[i : j + 1])
            i = j + 1
            continue
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def split_statements(src: str):
    """Yield statement chunks ended by ';' outside string literals."""
    buf = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == q:
                    break
                j += 1
            buf.append(src[i : j + 1])
            i = j + 1
            continue
        if c == ";":
            yield "".join(buf)
            buf = []
            i += 1
            continue
        buf.append(c)
        i += 1
    if buf:
        yield "".join(buf)


def mask_literals(stmt: str) -> str:
    """Same length as stmt, literal bodies replaced by '.' so regexes never
    see JavaScript that happens to contain '+='."""
    out = []
    i = 0
    n = len(stmt)
    while i < n:
        c = stmt[i]
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n:
                if stmt[j] == "\\":
                    j += 2
                    continue
                if stmt[j] == q:
                    break
                j += 1
            out.append(q + "." * max(0, j - i - 1) + q)
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


_ASSIGN = re.compile(r"(?:^|[\s;{}()])(?:String\s+)?([A-Za-z_]\w*)\s*(\+=|=)\s*")
_DECL = re.compile(r"(?:static\s+)?const\s+char\s*\*?\s*(?:const\s+)?([A-Za-z_]\w*)\s*(?:\[\s*\])?\s*(?:PROGMEM\s*)?=\s*")


def render_rhs(rhs: str) -> str:
    """Replay the right-hand side of an assignment into emitted text."""
    out = []
    i = 0
    n = len(rhs)
    while i < n:
        c = rhs[i]
        if c == '"':
            j = i + 1
            while j < n:
                if rhs[j] == "\\":
                    j += 2
                    continue
                if rhs[j] == '"':
                    break
                j += 1
            out.append(unescape(rhs[i + 1 : j]))
            i = j + 1
            continue
        if c == "'":
            j = i + 1
            while j < n and rhs[j] != "'":
                j += 2 if rhs[j] == "\\" else 1
            out.append("0")
            i = j + 1
            continue
        m = re.match(r"[A-Za-z_]\w*", rhs[i:])
        if m:
            name = m.group(0)
            i += len(name)
            k = i
            while k < n and rhs[k].isspace():
                k += 1
            if k < n and rhs[k] == "(":
                if name in ("F", "PSTR", "String"):
                    if name == "String":
                        out.append("0")
                        i = skip_parens(rhs, k)
                    else:
                        i = k + 1  # transparent wrapper, keep walking inside
                    continue
                depth = 0
                j = k
                while j < n:
                    if rhs[j] == '"':
                        j += 1
                        while j < n and rhs[j] != '"':
                            j += 2 if rhs[j] == "\\" else 1
                    elif rhs[j] == "(":
                        depth += 1
                    elif rhs[j] == ")":
                        depth -= 1
                        if depth == 0:
                            break
                    j += 1
                if name == "jsStr":
                    out.append('"x"')
                elif name == "webShellJsStrings":
                    out.append("0;")
                else:
                    out.append("0")
                i = j + 1
                continue
            if name not in ("true", "false"):
                out.append("0")
            continue
        i += 1
    return "".join(out)


def skip_parens(s: str, k: int) -> int:
    depth = 0
    j = k
    while j < len(s):
        if s[j] == '"':
            j += 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
        elif s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    return j


def _replay(stmt: str, buffers: dict, rename: dict) -> None:
    """One statement into the buffer it appends to; rename maps a helper's
    String parameter to the buffer of its caller."""
    masked = mask_literals(stmt)
    m = _DECL.search(masked)
    if m and '"' in stmt:
        name = m.group(1)
        buffers.setdefault(rename.get(name, name), []).append(render_rhs(stmt[m.end() :]))
        return
    m = _ASSIGN.search(masked)
    if not m:
        return
    if not ('"' in stmt[m.end() :] or "jsStr(" in stmt[m.end() :] or "(" in stmt[m.end() :]):
        return
    name = m.group(1)
    buffers.setdefault(rename.get(name, name), []).append(render_rhs(stmt[m.end() :]))


def reconstruct(path: str) -> dict:
    """Every buffer of a file, its statements in the order they stand in it."""
    src = strip_comments(open(path, encoding="utf-8").read())
    buffers: dict[str, list[str]] = {}
    for stmt in split_statements(src):
        _replay(stmt, buffers, {})
    return {k: "".join(v) for k, v in buffers.items()}


# A helper: a function whose one parameter is the String it appends to, like
# the printer page's scriptStrings(String& h). Its text belongs where it is
# called, not where it is defined.
#
# Wurzel: reconstruct() replays a file top to bottom. The printer page writes
#   <script> and </script> in pageScript() and its script in seven helpers
#   called between them, all defined above pageScript() and one in another
#   file (webPrinterHostScript). Their text landed before the tag, the block
#   between the tags was empty and blocks_for() skipped it: node never saw
#   that page (review of L1, Punkteliste f13).
# Fix: the check inlines a helper at each call `name(buf)` that stands as a
#   statement of its own, from the same file first, else from the one file
#   under src/web that defines it without `static`. Its body is not replayed
#   where it stands. reconstruct() stays as it was: test_download_embedded.py
#   reads every buffer of a file through it.
# Verworfen: replaying the helpers twice (where they stand and where they are
#   called) - a block could then hold the same declaration twice and node
#   would refuse it for the checker's doing.
# Test: test/test_check_web_js.py.
_HELPER = re.compile(r"(\bstatic\s+)?\bvoid\s+([A-Za-z_]\w*)\s*\(\s*String\s*&\s*([A-Za-z_]\w*)\s*\)\s*\{")
_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(\s*([A-Za-z_]\w*)\s*\)\s*$")
_INLINE_DEPTH = 8


def _helper_spans(src: str):
    """[(name, static, param, body_start, body_end)] of the helpers in a
    comment-free source; body_* are the offsets inside the braces."""
    masked = mask_literals(src)
    out = []
    for m in _HELPER.finditer(masked):
        open_at = m.end() - 1
        depth = 0
        for j in range(open_at, len(masked)):
            if masked[j] == "{":
                depth += 1
            elif masked[j] == "}":
                depth -= 1
                if depth == 0:
                    out.append((m.group(2), bool(m.group(1)), m.group(3), open_at + 1, j))
                    break
    return out


def collect_helpers(paths) -> dict:
    """{name: [(path, static, param, body)]} of the helpers in these files."""
    helpers: dict[str, list] = {}
    for path in paths:
        src = strip_comments(open(path, encoding="utf-8").read())
        for name, static, param, a, b in _helper_spans(src):
            helpers.setdefault(name, []).append((os.path.normpath(path), static, param, src[a:b]))
    return helpers


def _helper_for(helpers: dict, name: str, path: str):
    """The helper a call in path reaches: its own file's, else the only
    non-static one of that name. None when there is none or no single one."""
    found = helpers.get(name, [])
    own = [h for h in found if h[0] == os.path.normpath(path)]
    if own:
        return own[0]
    shared = [h for h in found if not h[1]]
    return shared[0] if len(shared) == 1 else None


def _replay_inlined(stmts, path, buffers, helpers, rename, depth) -> None:
    for stmt in stmts:
        m = _CALL.search(mask_literals(stmt))
        helper = _helper_for(helpers, m.group(1), path) if m else None
        if helper and depth < _INLINE_DEPTH:
            target = rename.get(m.group(2), m.group(2))
            hpath, _static, param, body = helper
            _replay_inlined(split_statements(body), hpath, buffers, helpers, {param: target}, depth + 1)
            continue
        _replay(stmt, buffers, rename)


def reconstruct_inlined(path: str, helpers: dict) -> dict:
    """Every buffer of a file as the page sends it: a helper's text where it
    is called. helpers comes from collect_helpers() over the files whose
    helpers the file may call."""
    src = strip_comments(open(path, encoding="utf-8").read())
    rest, last = [], 0
    for _name, _static, _param, a, b in _helper_spans(src):
        rest.append(src[last:a])
        last = b
    rest.append(src[last:])
    buffers: dict[str, list[str]] = {}
    _replay_inlined(split_statements("".join(rest)), path, buffers, helpers, {}, 0)
    return {k: "".join(v) for k, v in buffers.items()}


_SCRIPT = re.compile(r"<script(?:\s[^>]*)?>(.*?)</script>", re.S)


_PURE_JS = re.compile(r"^\s*[A-Za-z_$][\w$.]*\s*=[^=]")


def blocks_for(name: str, text: str):
    # A static JS literal is checked whole even when it compares with "<":
    # looking for <script> tags in it would find none and skip it silently.
    if name.endswith("JS"):
        yield 0, text
        return
    if "<" not in text:
        # Buffers that are JavaScript with no <script> tag around them: the
        # static app.js literal and the shell's WS strings. CSS buffers start
        # with a selector, never with an assignment, so they stay out.
        if name.endswith("JS") or _PURE_JS.match(text):
            yield 0, text
        return
    for n, m in enumerate(_SCRIPT.finditer(text), 1):
        body = m.group(1)
        if body.strip():
            yield n, body


def script_blocks(path: str, helpers: dict):
    """(buffer, number, script) of every block of a file that goes to node."""
    for name, text in reconstruct_inlined(path, helpers).items():
        for n, body in blocks_for(name, text):
            yield name, n, body


def main() -> int:
    node = shutil.which("node")
    if not node:
        print("check_web_js: node not found in PATH", file=sys.stderr)
        return 2
    files = []
    for dirpath, _dirs, names in os.walk(WEB):
        for f in sorted(names):
            if f.endswith(".cpp"):
                files.append(os.path.join(dirpath, f))
    failures = 0
    checked = 0
    helpers = collect_helpers(files)
    tmp = tempfile.mkdtemp(prefix="webjs-")
    for path in sorted(files):
        rel = os.path.relpath(path, ROOT)
        for name, n, body in script_blocks(path, helpers):
            checked += 1
            js = os.path.join(tmp, f"{os.path.basename(path)}.{name}.{n}.js")
            with open(js, "w", encoding="utf-8") as fh:
                fh.write(body)
            r = subprocess.run([node, "--check", js], capture_output=True, text=True)
            if r.returncode != 0:
                failures += 1
                msg = (r.stderr or r.stdout).strip().splitlines()
                print(f"FAIL {rel} buffer '{name}' script #{n}: {js}")
                for line in msg[:8]:
                    print("     " + (line if len(line) <= 200 else line[:200] + " ..."))
    print(f"check_web_js: {checked} script block(s) from {len(files)} file(s), {failures} failing")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
