"""Read the source tree without a compiler and check the search in the list of spool types (w01, V43): "Choose the spool type" gets the magnifier of the colour lists (ui/colour_search.cpp) with a keyboard title and a line for nothing found of its own, in the maker's list and in the whole one, not among the types near a weight. A word lists the types of the whole catalog that hold it, the maker's or not, filtered as the rows are built, and says so when none does; back ends a search first, a close forgets it, and so do "Show all" and "Only <maker>", so that the list and its tick agree: the tick is set on a row that is shown, never on one the search left out."""
import json
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

STL = "ui/spool_type_list.cpp"
CS_H = "ui/colour_search.h"
CS = "ui/colour_search.cpp"
NOT_ID = r"(?<![A-Za-z0-9_])"
END_ID = r"(?![A-Za-z0-9_])"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def source_of(rel):
    """One file under src/ as the ratchet reads it."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def code_of(rel):
    """The whole file, comments gone, string contents blanked, squeezed."""
    return squeeze(source_of(rel).code)


def functions_of(rel):
    """{name: [squeezed body, ...]} of every function the file defines."""
    code = source_of(rel).code
    out = {}
    for name, start, end in cc.find_functions(code):
        out.setdefault(name, []).append(squeeze(code[start + 1:end]))
    return out


def body_of(rel, name):
    """The code between the braces of the one function called name, squeezed."""
    bodies = functions_of(rel).get(name, [])
    assert len(bodies) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return bodies[0]


def close_of(code, i):
    """Offset of the bracket that closes the one at code[i], or -1."""
    o = code[i]
    c = {"(": ")", "{": "}", "[": "]"}[o]
    depth = 0
    for k in range(i, len(code)):
        if code[k] == o:
            depth += 1
        elif code[k] == c:
            depth -= 1
            if depth == 0:
                return k
    return -1


def open_of(code, i):
    """Offset of the bracket that opens the one at code[i], or -1."""
    c = code[i]
    o = {")": "(", "}": "{", "]": "["}[c]
    depth = 0
    for k in range(i, -1, -1):
        if code[k] == c:
            depth += 1
        elif code[k] == o:
            depth -= 1
            if depth == 0:
                return k
    return -1


def split_args(text):
    """The arguments of a call, split at the commas outside brackets."""
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return out


def calls_in(body, name):
    """[(offset, [arguments])] of every call of name in a squeezed body."""
    out = []
    for m in re.finditer(NOT_ID + name + r"\(", body):
        o = m.end() - 1
        out.append((m.start(), split_args(body[o + 1:close_of(body, o)])))
    return out


def head_conditions(code, brace):
    """The conditions a block opened at code[brace] runs under: its if (...)
    and, for an else or an else if, those of the blocks before it in the
    chain. A loop, a switch and a function head add none."""
    conds, end = [], brace
    while True:
        if code[:end].endswith(")"):
            o = open_of(code, end - 1)
            kw = code[:o]
            if kw.endswith("elseif"):
                conds.append(code[o + 1:end - 1])
                end = o - len("elseif")
            elif re.search(r"(^|[^A-Za-z0-9_])if$", kw):
                conds.append(code[o + 1:end - 1])
                return conds
            else:
                return conds
        elif re.search(r"(^|[^A-Za-z0-9_])else$", code[:end]):
            end -= len("else")
        else:
            return conds
        # Right before the else: the block of the branch before it.
        if end > 0 and code[end - 1] == "}":
            end = open_of(code, end - 1)
        else:
            return conds


def guards(code, pos):
    """What decides whether the statement at pos runs: the conditions of the
    blocks around it, a braceless if in front of it, and the statement itself
    (a ternary in it)."""
    conds, depth = [], 0
    for k in range(pos - 1, -1, -1):
        if code[k] == "}":
            depth += 1
        elif code[k] == "{":
            if depth == 0:
                conds += head_conditions(code, k)
            else:
                depth -= 1
    start = max(code.rfind(";", 0, pos), code.rfind("{", 0, pos), code.rfind("}", 0, pos)) + 1
    end = code.find(";", pos)
    conds.append(code[start:end if end >= 0 else len(code)])
    return conds


def mentions(conds, body, pattern):
    """Whether a condition holds the pattern, directly or through a local
    bool of the body it names."""
    local = re.findall(r"(?:^|[;{}])(?:const)?bool(\w+)=([^;]+);", body)
    for cond in conds:
        text = cond
        for name, value in local:
            if re.search(NOT_ID + name + END_ID, cond):
                text += " " + value
        if re.search(pattern, text):
            return True
    return False


def top_level_and(text):
    """Whether text holds an && outside brackets: only part of a case is meant."""
    depth = 0
    for k, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif depth == 0 and text.startswith("&&", k):
            return True
    return False


def early_returns(body, pos):
    """The conditions of the ifs before pos, in a block that still holds pos,
    whose branch returns: pos is only reached when they are false. One that
    holds an && (directly or through a local bool) is left out: it returns for
    part of a case only ("is_near && !near_ok")."""
    local = dict(re.findall(r"(?:^|[;{}])(?:const)?bool(\w+)=([^;]+);", body))
    out = []
    for m in re.finditer(NOT_ID + r"if\(", body[:pos]):
        o = m.end() - 1
        c = close_of(body, o)
        if c < 0 or c >= pos:
            continue
        if body[c + 1] == "{":
            e = close_of(body, c + 1)
            if e < 0 or e >= pos:
                continue   # pos is inside the branch: guards() has the condition
            branch = body[c + 1:e + 1]
        else:
            branch = body[c + 1:body.find(";", c + 1) + 1]
        if not re.search(NOT_ID + r"return" + END_ID, branch):
            continue
        depth, encloses = 0, True
        for ch in body[m.start():pos]:
            depth += {"{": 1, "}": -1}.get(ch, 0)
            if depth < 0:
                encloses = False
                break
        cond = body[o + 1:c]
        parts = [cond] + [v for n, v in local.items() if re.search(NOT_ID + n + END_ID, cond)]
        if encloses and not any(top_level_and(p) for p in parts):
            out.append(cond)
    return out


def with_helpers(conds, funcs, builders):
    """The conditions and the bodies of the small helpers of the file they
    call, two steps deep. Builders (a function that builds the list) are not
    followed: a redraw is no guard."""
    text, seen = " ".join(conds), set()
    for _ in range(2):
        for called in set(re.findall(NOT_ID + r"(\w+)\(", text)) - seen:
            seen.add(called)
            if called in funcs and called not in builders:
                text += " " + " ".join(funcs[called])
    return [text]


def inline_helpers(text, funcs, keep):
    """text with the body of every function of the file it calls (not those
    in keep) written in after the call, so that the order of what happens
    stays readable; one step deep."""
    out, at = "", 0
    for m in re.finditer(NOT_ID + r"(\w+)\(", text):
        name = m.group(1)
        if m.start() >= at and name in funcs and name not in keep:
            end = close_of(text, m.end() - 1)
            out += text[at:end + 1] + "{" + " ".join(funcs[name]) + "}"
            at = end + 1
    return out + text[at:]


def case_segment(body, label):
    """The statements of a switch case: from case label (and the labels that
    share its statements) to the next label of the same switch."""
    m = re.search(r"case" + label + r":", body)
    assert m, f"no case {label}"
    at = m.end()
    while True:
        nxt = re.match(r"case\w+:", body[at:])
        if not nxt:
            break
        at += nxt.end()
    depth = 0
    for k in range(at, len(body)):
        if body[k] == "{":
            depth += 1
        elif body[k] == "}":
            if depth == 0:
                return body[at:k]
            depth -= 1
        elif depth == 0 and re.match(r"(case\w+:|default:)", body[k:]) and not re.match(r"[A-Za-z0-9_]", body[k - 1]):
            return body[at:k]
    return body[at:]


# 1. The list keeps one search of the colour lists, with texts of its own.
stl_src = source_of(STL)
stl = code_of(STL)
assert re.search(r'#include\s+"(ui/)?colour_search\.h"', stl_src.text), f"src/{STL}: no colour search"
m = re.search(r"staticColourSearch(\w+)(=?\{[^;]*\})?;", stl)
assert m, f"src/{STL}: no static ColourSearch"
find, init = m.group(1), m.group(2) or ""
for field, sid in (("key_title", "STR_SPT_FIND_KEY"), ("none_fmt", "STR_SPT_FIND_NONE")):
    assert sid in init or f"{find}.{field}={sid};" in stl, \
        f"src/{STL}: the search {find} does not get {sid} as its {field}"

# 2. The header: the magnifier in the maker's list and in the whole list,
# built with the rows on every build (a mode switch changes the list), never
# among the types near a weight. What decides it may be the block around the
# call, its own argument, an if before it in the same function that returns
# for the near types, or a small helper of the file any of them calls. A call
# without a redraw only drops the head and may stand anywhere. The title it
# narrows is one the file holds, and it stays "Choose the spool type".
funcs = functions_of(STL)
on_build = {"stlBuild"}
for _ in range(2):
    for name in list(on_build):
        for called in re.findall(NOT_ID + r"(\w+)\(", "".join(funcs.get(name, []))):
            if called in funcs:
                on_build.add(called)
builders = {name for name, bodies in funcs.items()
            if any(re.search(r"colourSearchTitle\(|stlTypeRow\(|lv_obj_clean\(|lv_obj_create\(|(?<![A-Za-z0-9_])stlBuild"
                             r"(?![A-Za-z0-9_])", b) for b in bodies)}
titles = [(name, body, at, args) for name, bodies in funcs.items() for body in bodies
          for at, args in calls_in(body, "colourSearchTitle")]
assert titles, f"src/{STL}: colourSearchTitle() is never called: the list has no magnifier"
offered = 0
for name, body, at, args in titles:
    where = f"src/{STL}: {name}()"
    assert len(args) == 5 and args[0] == "&" + find, f"{where}: colourSearchTitle() is not called for {find}"
    title = args[2]
    if re.fullmatch(r"\w+", title):
        assert re.search(NOT_ID + title + r"=(?!=)(?!nullptr;)", stl), f"{where}: the title {title} is never set"
    if args[4] in ("nullptr", "NULL", "0"):
        continue
    assert name in on_build, f"{where}: colourSearchTitle() is not on the way of stlBuild(), so a mode switch keeps the old head"
    offered += 1
    decided = with_helpers(guards(body, at), funcs, builders)
    early = early_returns(body, at)
    returned = with_helpers(early, funcs, builders) if early else []
    assert mentions(decided, body, r"STL_NEAR") or mentions(returned, body, r"STL_NEAR") or \
        (mentions(decided, body, r"STL_VENDOR") and mentions(decided, body, r"STL_ALL")), \
        f"{where}: the search is offered among the types near a weight (no STL_NEAR in what decides it)"
assert offered, f"src/{STL}: colourSearchTitle() never gets a redraw: no list offers the search"
assert "STR_SPT_TITLE" in stl, f"src/{STL}: the list lost its title STR_SPT_TITLE"

# 3. The rows. With a word the maker's filter is off: every use of the
# maker's count depends on the word. A row is shown when it holds the word,
# decided before it is built; the tick is set on a built row only; nothing
# found is said across the list.
rows_at = [name for name, bodies in funcs.items() if name != "stlTypeRow"
           for body in bodies if re.search(NOT_ID + r"stlTypeRow\(", body)]
assert rows_at == ["stlBuildRows"], f"src/{STL}: the rows are built in {rows_at}, not in stlBuildRows() alone"
where = f"src/{STL}: stlBuildRows()"
body = body_of(STL, "stlBuildRows")
uses = [m.start() for m in re.finditer(NOT_ID + r"of_vendor" + END_ID, body)
        if not re.match(r"of_vendor=(?!=)", body[m.start():]) and not body[:m.start()].endswith("s_vendor_has=")]
assert uses, f"{where}: the maker's count (of_vendor) is not used for the rows"
for at in uses:
    assert mentions(guards(body, at), body, r"word"), \
        f"{where}: the maker's rows are taken whether or not a word is searched for (the whole catalog with a word)"
shows = body.find("colourSearchShows(")
built = body.find("stlTypeRow(")
assert shows >= 0, f"{where}: the rows are not filtered with colourSearchShows()"
assert shows < built, f"{where}: colourSearchShows() is asked after a row is built"
assert "colourSearchShows(&" + find + "," in body, f"{where}: colourSearchShows() is not asked for {find}"
for m in re.finditer(NOT_ID + r"marked=(?!=)(?!nullptr;)", body):
    assert m.start() > shows, f"{where}: the tick is set before the word decides the row"
assert "LV_OBJ_FLAG_HIDDEN" not in body, f"{where}: rows are hidden instead of left out"
none_at = [at for at, _ in calls_in(body, "colourSearchNone")]
assert len(none_at) == 1, f"{where}: colourSearchNone() is not called once"
assert none_at[0] > body.rfind("stlTypeRow("), f"{where}: colourSearchNone() comes before the rows"
assert mentions(guards(body, none_at[0]), body, r"(==0|<1)(?!\d)|!\(?(shown|rows)\b"), \
    f"{where}: colourSearchNone() is not for no row shown"

# 4. The tick carries out the head's tap and the word; back ends a search
# first; a close forgets it; "Show all" and "Only <maker>" end it before the
# list is built anew, so the new list is not cut by a word it no longer shows.
# colourSearchClose() forgets the head without deleting it (the screen it is
# on goes with a close); on these two the screen stays, so the head goes
# first: lv_obj_del() of it, or colourSearchTitle(..., nullptr), before the
# close, in the case or in a helper it calls.
assert "colourSearchTick(&" + find + "," in body_of(STL, "spoolTypeListTick"), \
    f"src/{STL}: spoolTypeListTick() does not call colourSearchTick()"
tick = body_of(STL, "spoolTypeListTick")
back = case_segment(tick, "STL_ACT_BACK")
if "colourSearchBack(" in back:
    assert back.find("colourSearchBack(&" + find + ")") >= 0 and back.find("colourSearchBack(") < back.find("stlLeave("), \
        f"src/{STL}: spoolTypeListTick(): back leaves the list before it ends a search"
else:
    leave = body_of(STL, "stlLeave")
    at = leave.find("colourSearchBack(&" + find + ")")
    assert 0 <= at < leave.find("closeSpoolTypeList("), \
        f"src/{STL}: back does not end a search first (colourSearchBack() in the back case or stlLeave())"
    assert mentions(guards(leave, at), leave, r"back"), f"src/{STL}: stlLeave() ends a search on skip as well"
assert "colourSearchClose(&" + find + ")" in body_of(STL, "closeSpoolTypeList"), \
    f"src/{STL}: closeSpoolTypeList() does not forget the search"
for act in ("STL_ACT_SHOW_ALL", "STL_ACT_ONLY_VENDOR"):
    seg = inline_helpers(case_segment(tick, act), funcs, on_build | {"stlBuild"})
    where = f"src/{STL}: spoolTypeListTick(): {act}"
    at = seg.find("colourSearchClose(&" + find + ")")
    assert 0 <= at < seg.find("stlBuild("), f"{where} builds the list without ending the search first"
    before = seg[:at]
    drops = [m.start() for m in re.finditer(r"lv_obj_del\((" + find + r"\.|\w+->)head\)", before)]
    drops += [p for p, a in calls_in(before, "colourSearchTitle")
              if a[0] in ("&" + find, "s") and a[-1] in ("nullptr", "NULL", "0")]
    assert drops, f"{where} forgets the head without deleting it: the old word stays drawn (lv_obj_del() of the " \
                  f"head or colourSearchTitle(&{find}, ..., nullptr) before colourSearchClose())"

# 5. The shared part: two text ids in the search, zero for the colour texts,
# so that the colour lists stay as they are.
struct = re.search(r"structColourSearch\{(.*?)\};", code_of(CS_H))
assert struct, f"src/{CS_H}: no struct ColourSearch"
for field in ("key_title", "none_fmt"):
    assert f"int{field}=0;" in struct.group(1), f"src/{CS_H}: ColourSearch has no 'int {field} = 0;'"
cs = code_of(CS)
for field, colour in (("key_title", "STR_COLOUR_FIND_KEY"), ("none_fmt", "STR_COLOUR_FIND_NONE")):
    assert "->" + field in cs and colour in cs, \
        f"src/{CS}: the text comes not from s->{field} with {colour} for a search without one"

# 6. Two new texts, in all three languages, the French file and the budgets.
lang_h = (src / "lang.h").read_text(encoding="utf-8")
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
fr = {}
for line in (root / "tools" / "lang_fr.jsonl").read_text(encoding="utf-8").splitlines():
    if line.strip():
        row = json.loads(line)
        fr[row["id"]] = row
budgets = (root / "tools" / "ui_budgets.tsv").read_text(encoding="utf-8")
CONV = r"%[-+ #0-9.]*[a-zA-Z]"
for sid, conversions in (("STR_SPT_FIND_KEY", []), ("STR_SPT_FIND_NONE", ["%s"])):
    assert re.search(r"^\s*" + sid + r"\s*,", lang_h, re.M), f"src/lang.h: no {sid}"
    row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                    + sid + r"\b", lang_cpp)
    assert row, f"src/lang.cpp: no row of three texts for {sid}"
    for lang, text in zip(("de", "en", "fr"), row.groups()):
        assert text.strip(), f"src/lang.cpp: {sid} has no {lang} text"
        assert re.findall(CONV, text) == conversions, f"src/lang.cpp: {sid} ({lang}) has not {conversions or 'no conversion'}"
        assert sid in fr and fr[sid].get(lang) == text, f"tools/lang_fr.jsonl: {sid} ({lang}) is not the text of lang.cpp"
    assert re.search(r"^" + sid + r"\t\d+\t\d+\t(NEVER|DOT|WRAP:\d+)\t", budgets, re.M), \
        f"tools/ui_budgets.tsv: no budget for {sid}"

print("list search: ok")
