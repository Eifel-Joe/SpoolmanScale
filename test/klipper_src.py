"""Shared by the Klipper source tests (not a test itself, nothing runs here): the source tree read without a compiler, the way test_ams_info.py reads it. Comments and string contents are blanked for the code view (function bodies by brace matching, found with the convention ratchet's tokenizer), the literals stay in the string view; calls are split into their arguments; the string table is read as the French tool reads it, rows of German, English and French with the id from the comment behind them. Python puts the folder of the script that runs on sys.path, so a test imports this as `import klipper_src as ks`."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer and the function finder of the convention ratchet. The script
# is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402


def collapse(text):
    """Runs of blanks as one blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", " ", text).strip()


def squeeze(text):
    """The text without any blank."""
    return re.sub(r"\s+", "", text)


def source_of(rel):
    """One file under src/ as the ratchet's Source (code and code_str views)."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def code_of(rel):
    """The code of one file (comments and string contents blanked), blanks collapsed."""
    return collapse(source_of(rel).code)


def body_of(rel, name, strings=False):
    """The text between the braces of the one function called name, blanks
    collapsed. strings=True keeps the string literals (comments are blank
    either way)."""
    source = source_of(rel)
    spans = [(a, b) for found, a, b in cc.find_functions(source.code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    view = source.code_str if strings else source.code
    return collapse(view[spans[0][0] + 1:spans[0][1]])


def literals_of(rel):
    """The contents of every string literal of a file (quotes off, raw strings
    included), one entry each."""
    source = source_of(rel)
    out = []
    for start, end in source.strings:
        lit = source.text[start:end]
        if lit.startswith("R"):
            lit = lit[lit.index("(") + 1:lit.rindex(")")]
        else:
            lit = lit[1:-1]
        out.append(lit)
    return out


def calls(text, name):
    """[(offset, [argument texts])] of every call name( ... ) in text, split at
    the commas of the top level, string literals skipped over."""
    out = []
    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        depth, args, cur, j, quote = 1, [], [], m.end(), None
        while j < len(text):
            ch = text[j]
            if quote:
                cur.append(ch)
                if ch == "\\" and j + 1 < len(text):
                    cur.append(text[j + 1])
                    j += 1
                elif ch == quote:
                    quote = None
            elif ch in "\"'":
                quote = ch
                cur.append(ch)
            elif ch in "([{":
                depth += 1
                cur.append(ch)
            elif ch in ")]}":
                depth -= 1
                if depth == 0:
                    break
                cur.append(ch)
            elif ch == "," and depth == 1:
                args.append("".join(cur).strip())
                cur = []
            else:
                cur.append(ch)
            j += 1
        tail = "".join(cur).strip()
        if tail or args:
            args.append(tail)
        out.append((m.start(), args))
    return out


def call_spans(text, name):
    """[(start, end)] of every call name( ... ) in text, from the name to the
    closing parenthesis."""
    out = []
    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        depth, j, quote = 1, m.end(), None
        while j < len(text) and depth:
            ch = text[j]
            if quote:
                if ch == "\\":
                    j += 1
                elif ch == quote:
                    quote = None
            elif ch in "\"'":
                quote = ch
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            j += 1
        out.append((m.start(), j))
    return out


def calls_with_literal(text, literal):
    """[(offset, name, text of the arguments)] of the innermost call around each
    appearance of the string literal (quotes included) in text, the way
    request("POST", c, key, "/path", body) is the call that names "/path"."""
    out = []
    for m in re.finditer(r"\b(\w+)\s*\(", text):
        depth, j, quote = 1, m.end(), None
        while j < len(text) and depth:
            ch = text[j]
            if quote:
                if ch == "\\":
                    j += 1
                elif ch == quote:
                    quote = None
            elif ch in "\"'":
                quote = ch
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            j += 1
        args = text[m.end():j - 1]
        if literal in args:
            out.append((m.start(), m.group(1), args))
    # An outer call that holds the literal only inside an inner call is not the
    # call that names it: keep a call when no other candidate lies inside it.
    keep = []
    for off, name, args in out:
        inner = [o for o, n, a in out if o > off and o < off + len(name) + len(args) + 2 and (o, n, a) != (off, name, args)]
        if not inner:
            keep.append((off, name, args))
    return keep


def route_spans(rel, path):
    """[(start, end)] of every srv.on("path", ...) call of a page, from the name
    of the server object to the closing parenthesis, as offsets into the
    file's code. A path can have several: one for GET and one for POST."""
    source = source_of(rel)
    out = []
    for off, p, _ in cc.routes(source):
        if p.strip('"') != path:       # the ratchet hands the literal with its quotes
            continue
        open_idx = source.code.index("(", off)
        close_idx = cc.match_close(source.code, open_idx, cc._PARENS, "(")
        assert close_idx > 0, f"src/{rel}: the route {path} is not closed"
        out.append((off, close_idx + 1))
    assert out, f"src/{rel}: no route {path}"
    return out


def route_text(rel, path):
    """The text (string literals kept) of all the calls of the route, joined."""
    source = source_of(rel)
    return collapse(" ".join(source.code_str[a:b] for a, b in route_spans(rel, path)))


def log_leaks(rel):
    """(how many log calls the file has, [the calls that carry a key]): every
    argument of logSD() and logSDf() after the format, with the string literals
    taken out, is searched for the names `key` and `keys`. The one thing about a
    key a line may say is whether there is one: key ? "set" : "empty"."""
    text = source_of(rel).code_str
    found, leaks = 0, []
    for fn in ("logSDf", "logSD"):
        for pos, args in calls(text, fn):
            found += 1
            for arg in (args[1:] if fn == "logSDf" else args):
                if re.fullmatch(r'[^"]*\?\s*"(?:set|empty)"\s*:\s*"(?:set|empty)"', arg):
                    continue
                bare = re.sub(r'"(?:[^"\\]|\\.)*"', '""', arg)
                if re.search(r"\b(?:key|keys)\b", bare):
                    leaks.append(f"{fn}(...{arg}...) at offset {pos}")
    return found, leaks


# ---------------------------------------------------------------- string table

_TOK = re.compile(r'"(?:[^"\\]|\\.)*"|//[^\n]*|/\*.*?\*/|[A-Za-z_]\w*|[{},;]|\s+|.', re.S)


def _unescape(lit):
    table = {"n": "\n", "t": "\t", "r": "\r"}
    return re.sub(r"\\([\"\\'ntr])", lambda m: table.get(m.group(1), m.group(1)), lit)


def lang_rows():
    """{string id: [German, English, French]} of src/lang.cpp, read as
    scripts/check.sh reads it (rule 6): tokenized, so a text split over
    several literals is one text. Names of macros between the literals
    (LV_SYMBOL_...) are left out of the text."""
    text = (src / "lang.cpp").read_text(encoding="utf-8")
    body = text[text.index("{", text.index("STRINGS[]")) + 1:]
    rows, ids, depth, cell, cells = [], [], 0, [], None
    for tok in _TOK.findall(body):
        if tok.startswith("//"):
            m = re.search(r"STR_\w+", tok)
            if m and depth == 0 and len(ids) < len(rows):
                ids.append(m.group(0))
            continue
        if tok.startswith("/*") or tok.isspace():
            continue
        if tok == "{":
            depth += 1
            cells, cell = [], []
            while len(ids) < len(rows):
                ids.append("?")
            continue
        if tok == "}":
            if depth == 0:
                break
            depth -= 1
            cells.append(cell)
            rows.append(cells)
            continue
        if depth == 0:
            continue
        if tok == ",":
            cells.append(cell)
            cell = []
        else:
            cell.append(tok)
    while len(ids) < len(rows):
        ids.append("?")
    out = {}
    for sid, row in zip(ids, rows):
        out[sid] = ["".join(_unescape(t[1:-1]) for t in c if t.startswith('"')) for c in row]
    return out


def lang_ids_with_german(rows, pattern):
    """The string ids whose German text matches the regular expression whole."""
    return [sid for sid, cells in rows.items() if cells and re.fullmatch(pattern, cells[0])]


def assert_row_complete(rows, sid, where):
    """The row has a German, an English and a French text, and none has the
    em-dash that breaks the Xtensa compiler and the panel's font."""
    assert sid in rows, f"{where}: {sid} has no row in src/lang.cpp"
    assert len(rows[sid]) == 3 and all(c.strip() for c in rows[sid]), \
        f"{where}: the row of {sid} lacks a German, an English or a French text"
    for cell in rows[sid]:
        assert chr(0x2014) not in cell, f"{where}: the row of {sid} has an em-dash"


def enum_has(sid):
    """The id is an entry of the string enum in src/lang.h."""
    header = (src / "lang.h").read_text(encoding="utf-8")
    return re.search(rf"\b{sid}\b\s*,", header) is not None


# ----------------------------------------------------------------------- CI

def ci_runs():
    """The commands of the CI file's steps, in order: [(name, run)]."""
    text = (root / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    return re.findall(r"- name: ([^\n]*)\n(?:\s+if: [^\n]*\n)?\s+run: ([^\n]*)", text)


def assert_ci_step(script):
    """A step of the house-rules job runs python3 test/<script>."""
    want = f"python3 test/{script}"
    assert any(run.strip() == want for _, run in ci_runs()), \
        f".github/workflows/ci.yml: no step runs '{want}'"
