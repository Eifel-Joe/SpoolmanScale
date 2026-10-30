"""Read the source tree without a compiler and check the window "Choose printer" (beta.9, V47): the printer chip of the AMS window opens a window with one button per printer in two columns (name above, kind under it: the Bambu model, or Klipper with its layout), a magnifier that is always there, the tick on the current printer and "Cancel" in the style of the original, instead of stepping to the next printer. The window shows the list the AMS window has loaded and asks for nothing; the AMS window stays under it, hidden, so that the countdown of its "Cancel" stands; the chip stays when the chosen printer does not load.

Each check names the shape of code it expects in a comment above it. The checks run in the order of the tasks that make them pass (A, B: the model of a Bambu printer; C to G and J: the window and its texts; H, I: the AMS window; K: the chip that stays, which beta.8 has already), and the script stops at the first one that fails."""
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

SLOTS = "services/ams_slots.h"
BB = "services/bambuddy_api.cpp"
PL_H = "ui/printer_list.h"
PL = "ui/printer_list.cpp"
AV = "ui/ams_view.cpp"
NOT_ID = r"(?<![A-Za-z0-9_])"
END_ID = r"(?![A-Za-z0-9_])"
HIDDEN_ON = r"lv_obj_add_flag\(s_scr,LV_OBJ_FLAG_HIDDEN\)"
HIDDEN_OFF = r"lv_obj_clear_flag\(s_scr,LV_OBJ_FLAG_HIDDEN\)"
# What a handler must not do: it parks, the tick builds and frees.
WIDGET_CALLS = r"(?:lv_obj_create|lv_btn_create|lv_label_create|lv_obj_clean|lv_obj_del\w*)\("


# ---------------------------------------------------------------------------
#  Helpers: the reading of a source file (as test_list_search.py has them)
# ---------------------------------------------------------------------------

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


def functions_of(rel, strings=False):
    """{name: [squeezed body, ...]} of every function the file defines. With
    strings the string literals stay as written (a key of a JSON object)."""
    s = source_of(rel)
    text = s.code_str if strings else s.code
    out = {}
    for name, start, end in cc.find_functions(s.code):
        out.setdefault(name, []).append(squeeze(text[start + 1:end]))
    return out


def body_of(rel, name, strings=False):
    """The code between the braces of the one function called name, squeezed."""
    bodies = functions_of(rel, strings).get(name, [])
    assert len(bodies) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return bodies[0]


def params_of(rel, name):
    """The names of the parameters of the one function called name, in order."""
    code = source_of(rel).code
    heads = []
    for n, start, _ in cc.find_functions(code):
        if n == name:
            begin = max(code.rfind(";", 0, start), code.rfind("{", 0, start), code.rfind("}", 0, start)) + 1
            heads.append(code[begin:start])
    assert len(heads) == 1, f"src/{rel}: {name}() is not defined exactly once"
    m = re.search(NOT_ID + re.escape(name) + r"\s*\(", heads[0])
    assert m, f"src/{rel}: the head of {name}() is not readable"
    o = m.end() - 1
    inside = heads[0][o + 1:close_of(heads[0], o)]
    names = []
    for part in split_args(inside):
        ids = re.findall(r"[A-Za-z_]\w*", part.split("=")[0])
        names.append(ids[-1] if ids else "")
    return [] if names == ["void"] else names


def define_of(rel, name):
    """The value of a #define of the file, brackets off, or None."""
    m = re.search(r"^[ \t]*#[ \t]*define[ \t]+" + re.escape(name) + r"[ \t]+(\S[^\n]*?)[ \t\r]*$",
                  source_of(rel).code, re.M)
    return m.group(1).strip("() \t") if m else None


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
    (a ternary in it). The statement is the last of the list."""
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


def early_exits(body, pos, exits=r"return"):
    """The conditions of the ifs before pos, in a block that still holds pos,
    whose branch leaves (exits: return, or continue and break in a loop): pos
    is only reached when they are false. One that holds an && (directly or
    through a local bool) is left out: it leaves for part of a case only."""
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
        if not re.search(NOT_ID + r"(?:" + exits + r")" + END_ID, branch):
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


def loops_in(body):
    """[(index variable, bound, body of the loop)] of every for loop of a
    squeezed body whose condition reads "<variable> < <bound>"."""
    out = []
    for m in re.finditer(NOT_ID + r"for\(", body):
        o = m.end() - 1
        c = close_of(body, o)
        parts = body[o + 1:c].split(";")
        cond = re.fullmatch(r"\(?(\w+)\)?<=?(.+)", parts[1]) if len(parts) == 3 else None
        if not cond:
            continue
        if body[c + 1] == "{":
            inner = body[c + 2:close_of(body, c + 1)]
        else:
            inner = body[c + 1:body.find(";", c + 1) + 1]
        out.append((cond.group(1), cond.group(2), inner))
    return out


CAST_RE = re.compile(r"\((?:void\*|u?intptr_t|size_t|u?int(?:8|16|32)_t|int|long|unsigned|unsignedint)\)"
                     r"|(?:reinterpret|static)_cast<[^>]*>")


def bare(arg):
    """The argument without its casts and brackets: what is passed, by name."""
    return CAST_RE.sub("", arg).strip("()")


def on_build_of(funcs, root_fn):
    """The functions on the way of the one that builds the list: it, what it
    calls and what that calls."""
    on_build = {root_fn}
    for _ in range(2):
        for name in list(on_build):
            for called in re.findall(NOT_ID + r"(\w+)\(", "".join(funcs.get(name, []))):
                if called in funcs:
                    on_build.add(called)
    return on_build


def find_of():
    """(name, initialiser) of the one ColourSearch the window keeps."""
    found = re.findall(r"staticColourSearch(\w+)(=?\{[^;]*\})?;", code_of(PL))
    assert len(found) == 1, f"src/{PL}: the window does not keep exactly one static ColourSearch"
    return found[0][0], found[0][1] or ""


def null_check(cond):
    """Whether cond only asks if something is there: "s_scr", "!s_list", "s_scr!=nullptr"."""
    one = r"!?\w+(?:(?:==|!=)(?:nullptr|NULL|0))?"
    return re.fullmatch(one + r"(?:(?:&&|\|\|)" + one + r")*", cond) is not None


def build_loop():
    """(function, index variable, bound, body of the loop, body of the function)
    of the loop over the printers: the one that asks printerPickShows()."""
    found = []
    for name, bodies in functions_of(PL).items():
        for body in bodies:
            for var, bound, inner in loops_in(body):
                if re.search(NOT_ID + r"printerPickShows\(", inner):
                    found.append((name, var, bound, inner, body))
    assert found, f"src/{PL}: printerPickShows() is not asked in a loop: the buttons are not filtered by the rule"
    return min(found, key=lambda f: len(f[3]))   # the inner one, should two loops hold it


def build_start(loop, funcs):
    """Offset of the first thing in the loop that makes a button: a widget
    created, a callback attached, or a call of a function of the file that
    does either. -1 when there is none."""
    makers = {n for n, bs in funcs.items()
              if any(re.search(r"lv_btn_create\(|lv_obj_create\(|lv_obj_add_event_cb\(", b) for b in bs)}
    at = [m.start() for m in re.finditer(r"lv_btn_create\(|lv_obj_create\(|lv_obj_add_event_cb\(", loop)]
    for name in makers:
        at += [p for p, _ in calls_in(loop, name)]
    return min(at) if at else -1


# ---------------------------------------------------------------------------
#  A, B: the model of a Bambu printer (Task 2)
# ---------------------------------------------------------------------------

# A: services/ams_slots.h has "#define AMS_PRINTER_MODEL_MAX 12" and the struct
# AmsPrinter a member "char model[AMS_PRINTER_MODEL_MAX];" (zero for a Klipper
# printer, FilaMan and an AMS source, like name's neighbours).
def check_a():
    assert define_of(SLOTS, "AMS_PRINTER_MODEL_MAX") == "12", \
        f"src/{SLOTS}: no '#define AMS_PRINTER_MODEL_MAX 12' (a Bambu model is a few characters, 11 and a terminator)"
    struct = re.search(r"structAmsPrinter\{(.*?)\};", code_of(SLOTS))
    assert struct, f"src/{SLOTS}: no struct AmsPrinter"
    assert "charmodel[AMS_PRINTER_MODEL_MAX];" in struct.group(1), \
        f"src/{SLOTS}: AmsPrinter has no 'char model[AMS_PRINTER_MODEL_MAX];' for the model Bambuddy lists"


# B: in bbListPrinters() a call whose first argument is dst.model reads
# po["model"] with sizeof(dst.model) - 1, e.g.
#   strncpy(dst.model, po["model"] | "", sizeof(dst.model) - 1);
# (dst is zeroed before, so the copy ends in a terminator.)
def check_b():
    body = body_of(BB, "bbListPrinters", strings=True)
    copies = []
    for m in re.finditer(NOT_ID + r"(\w+)\(dst\.model,", body):
        o = m.end(1)
        copies.append(",".join(split_args(body[o + 1:close_of(body, o)])[1:]))
    assert copies, f"src/{BB}: bbListPrinters() does not copy anything into dst.model: the model is not read"
    assert any('po["model"]' in args and "sizeof(dst.model)-1" in args for args in copies), \
        f"src/{BB}: bbListPrinters() does not copy po[\"model\"] into dst.model with sizeof(dst.model) - 1"


# ---------------------------------------------------------------------------
#  C to G: the window (Task 3)
# ---------------------------------------------------------------------------

# C: ui/printer_list.h declares the four functions with these types (the names
# of the parameters are free):
#   void showPrinterList(const AmsPrinterList* list, int current_id,
#                        void (*on_pick)(int printer_id), void (*on_leave)());
#   void printerListTick();  bool isPrinterListOpen();  void closePrinterList();
def check_c():
    code = code_of(PL_H)
    decls = (
        ("showPrinterList(const AmsPrinterList* list, int current_id, void (*on_pick)(int printer_id), void (*on_leave)())",
         r"voidshowPrinterList\(constAmsPrinterList\*\w*,int\w*,void\(\*\w*\)\(int\w*\),void\(\*\w*\)\(\)\);"),
        ("printerListTick()", r"voidprinterListTick\((?:void)?\);"),
        ("isPrinterListOpen()", r"boolisPrinterListOpen\((?:void)?\);"),
        ("closePrinterList()", r"voidclosePrinterList\((?:void)?\);"),
    )
    for text, pattern in decls:
        assert re.search(pattern, code), f"src/{PL_H}: {text} is not declared"


# D: ui/printer_list.cpp keeps one "static ColourSearch s_find" with the texts
# of its own (in the initialiser, or "s_find.key_title = STR_PRL_FIND_KEY;"
# and "s_find.none_fmt = STR_PRL_FIND_NONE;" in the code). The function prlBuild()
# builds the list and, on its way, calls
#   colourSearchTitle(&s_find, s_scr, s_title, T(STR_PRL_TITLE), prlBuild);
# with no if around it and no ternary in it: the magnifier is always there. The
# list flows in rows that wrap (lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_ROW_WRAP)),
# #define PRL_CELL_W 224 and PRL_CELL_H 56, and the gap between the columns is 8:
# lv_obj_set_style_pad_column(s_list, 8, 0) or, with a #define PRL_CELL_GAP_X 8,
# lv_obj_set_style_pad_column(s_list, PRL_CELL_GAP_X, 0).
def check_d():
    find, init = find_of()
    pl = code_of(PL)
    for field, sid in (("key_title", "STR_PRL_FIND_KEY"), ("none_fmt", "STR_PRL_FIND_NONE")):
        assert sid in init or f"{find}.{field}={sid};" in pl, \
            f"src/{PL}: the search {find} does not get {sid} as its {field}"
    funcs = functions_of(PL)
    body_of(PL, "prlBuild")   # defined exactly once
    on_build = on_build_of(funcs, "prlBuild")
    titles = [(name, body, at, args) for name, bodies in funcs.items() for body in bodies
              for at, args in calls_in(body, "colourSearchTitle")]
    assert titles, f"src/{PL}: colourSearchTitle() is never called: the window has no magnifier"
    for name, body, at, args in titles:
        where = f"src/{PL}: {name}()"
        assert len(args) == 5 and args[0] == "&" + find and "STR_PRL_TITLE" in args[3] and args[4] == "prlBuild", \
            f"{where}: colourSearchTitle() is not called as colourSearchTitle(&{find}, scr, title, T(STR_PRL_TITLE), prlBuild)"
        assert name in on_build, f"{where}: colourSearchTitle() is not on the way of prlBuild(), so a redraw keeps the old head"
        # A check that the screen or the list is there is no condition on the magnifier.
        conds = guards(body, at)
        assert conds[-1].startswith("colourSearchTitle(") and "?" not in conds[-1] \
            and all(null_check(c) for c in conds[:-1] + early_exits(body, at)), \
            f"{where}: colourSearchTitle() runs under a condition (the magnifier is there for every list)"
    assert re.search(NOT_ID + r"LV_FLEX_FLOW_ROW_WRAP" + END_ID, pl), \
        f"src/{PL}: the list does not flow with LV_FLEX_FLOW_ROW_WRAP (two columns of buttons)"
    for name, value in (("PRL_CELL_W", "224"), ("PRL_CELL_H", "56")):
        assert define_of(PL, name) == value, f"src/{PL}: no '#define {name} {value}' (the button, 224 x 56)"
    gaps = [args[1] for bodies in funcs.values() for body in bodies
            for _, args in calls_in(body, "lv_obj_set_style_pad_column") if len(args) == 3]
    assert "8" in gaps or ("PRL_CELL_GAP_X" in gaps and define_of(PL, "PRL_CELL_GAP_X") == "8"), \
        f"src/{PL}: the gap between the columns is not 8 (lv_obj_set_style_pad_column(list, 8, 0), or PRL_CELL_GAP_X 8)"


# E: in prlBuild() or a helper of it a for loop runs over the whole list; its
# index i is the place in list->p:
#   for (int i = 0; i < list->count; i++) {
#     const PrinterPickRow row = prlRowOf(list->p[i]);
#     char art[PRINTER_ART_MAX];
#     printerPickArt(row, texts, art, sizeof(art));
#     if (!printerPickShows(row, art, s_find.word)) continue;   // the one filter
#     ...room guard (F)...
#     the button, with lv_obj_add_event_cb(btn, prlPickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
#   }
# The user data is that index, not a count of the buttons shown (the buttons of
# a word are fewer than the printers, and the tick reads list->p[index]). The
# button may be made by a helper that gets i as an argument and attaches the
# callback with that parameter. LV_SYMBOL_OK is set only where the printer's id
# equals the id showPrinterList() was given (kept in a static, or the parameter
# itself): a condition around it, a bool the condition names, or the argument
# of a helper that gets the comparison.
def check_e():
    find, _ = find_of()
    pl = code_of(PL)
    for banned in ("colourSearchShows", "colourSearchMatch", "colourSearchFdb"):
        assert not re.search(NOT_ID + banned + r"\(", pl), \
            f"src/{PL}: {banned}() filters the buttons: printerPickShows() is the only filter (name and kind)"
    funcs = functions_of(PL)
    name, var, bound, loop, fbody = build_loop()
    where = f"src/{PL}: {name}()"
    assert "count" in bound or re.search(re.escape(bound) + r"=[^;]*count", fbody), \
        f"{where}: the loop does not run over the whole list (its bound {bound} is not the count)"
    assert re.search(r"(?:->|\.)p\[" + var + r"\]", loop), \
        f"{where}: the loop does not read the printer at its index (list->p[{var}])"

    # The kind is asked before the word is, from the same buffer, and the word is the search's.
    arts, shows = calls_in(loop, "printerPickArt"), calls_in(loop, "printerPickShows")
    assert arts, f"{where}: the kind of a printer does not come from printerPickArt()"
    (art_at, art_args), (shows_at, shows_args) = arts[0], shows[0]
    assert len(art_args) == 4 and len(shows_args) == 3, f"{where}: printerPickArt() or printerPickShows() has the wrong number of arguments"
    assert art_at < shows_at and shows_args[1] == art_args[2], \
        f"{where}: printerPickShows() does not get the text printerPickArt() wrote ({art_args[2]})"
    assert shows_args[2] == find + ".word", f"{where}: printerPickShows() is not asked for the word of {find} ({find}.word)"

    # A printer the word leaves out gets no button: the filter decides before one is made.
    made = build_start(loop, funcs)
    assert made >= 0, f"{where}: the loop makes no button"
    assert shows_at < made, f"{where}: printerPickShows() is asked after a button is made"
    decided = guards(loop, made) + early_exits(loop, made, r"continue|break|return")
    assert mentions(decided, loop, r"printerPickShows\("), \
        f"{where}: a printer printerPickShows() leaves out still gets a button (no continue, no if around the button)"

    # The callback of the button carries the place in the list.
    carried = False
    for _, args in calls_in(loop, "lv_obj_add_event_cb"):
        carried = carried or (len(args) == 4 and args[2] == "LV_EVENT_CLICKED" and bare(args[3]) == var)
    for helper in funcs:
        for _, args in calls_in(loop, helper):
            params = params_of(PL, helper)
            for k, arg in enumerate(args):
                if bare(arg) == var and k < len(params):
                    for body in funcs[helper]:
                        for _, cb in calls_in(body, "lv_obj_add_event_cb"):
                            carried = carried or (len(cb) == 4 and cb[2] == "LV_EVENT_CLICKED" and bare(cb[3]) == params[k])
    assert carried, f"{where}: the click callback does not get the place in list->p ({var}) as its user data " \
                    f"(a count of the buttons shown is not the place of a printer)"

    # The tick: only on a built button, only for the current printer.
    shown = params_of(PL, "showPrinterList")
    assert len(shown) == 4, f"src/{PL}: showPrinterList() does not take the four parameters of the header"
    current = {shown[1]}
    for body in funcs.get("showPrinterList", []):
        current |= set(re.findall(r"([\w.>\[\]-]+)=" + re.escape(shown[1]) + r"[;,)]", body))
    names = "|".join(re.escape(n) for n in sorted(current))
    same = rf"(?:(?<![\w])id|[.>]id)==\(?(?:{names})(?![\w])|(?<![\w])(?:{names})\)?==[^&|;]*(?<![\w])id(?![\w])"
    on_build = on_build_of(funcs, "prlBuild")
    ticks = [(n, body, m.start()) for n, bodies in funcs.items() for body in bodies
             for m in re.finditer(NOT_ID + r"LV_SYMBOL_OK" + END_ID, body)]
    assert ticks, f"src/{PL}: LV_SYMBOL_OK is never set: the current printer carries no tick"
    for n, body, at in ticks:
        w = f"src/{PL}: {n}()"
        assert n in on_build, f"{w}: the tick is set outside the build of the list"
        conds = guards(body, at) + early_exits(body, at)
        ok = mentions(conds, body, same)
        if not ok:   # the comparison may be an argument of the function this one is called with
            for p in params_of(PL, n):
                for cbodies in funcs.values():
                    for cbody in cbodies:
                        for _, cargs in calls_in(cbody, n):
                            k = params_of(PL, n).index(p)
                            if k < len(cargs):
                                swapped = [re.sub(NOT_ID + re.escape(p) + END_ID, "(" + cargs[k] + ")", c) for c in conds]
                                ok = ok or mentions(swapped, cbody, same)
        assert ok, f"{w}: LV_SYMBOL_OK is set without asking whether the printer's id is the current one (id == current_id)"


# F: in the loop of E, after the filter and before the button:
#   if (!lvPoolHasRoomForRow()) { <a line with T(STR_PRL_MORE)>; break; }
# (a helper of the file may write the line; the loop ends with break or return).
def check_f():
    funcs = functions_of(PL)
    name, _, _, loop, _ = build_loop()
    where = f"src/{PL}: {name}()"
    guard = re.search(r"if\(!lvPoolHasRoomForRow\(\)\)", loop)
    assert guard, f"{where}: no 'if (!lvPoolHasRoomForRow())' in the loop: a button is made whether the pool has room or not"
    c = guard.end()
    branch = loop[c + 1:close_of(loop, c)] if loop[c] == "{" else loop[c:loop.find(";", c) + 1]
    assert guard.start() > calls_in(loop, "printerPickShows")[0][0], \
        f"{where}: the room is asked before the filter: a printer the word leaves out could cut the list"
    assert guard.start() < build_start(loop, funcs), f"{where}: the room is asked after a button is made"
    assert "STR_PRL_MORE" in inline_helpers(branch, funcs, set()), \
        f"{where}: without room the loop does not write the line STR_PRL_MORE"
    assert re.search(NOT_ID + r"(?:break|return)" + END_ID, branch), \
        f"{where}: without room the loop does not end (break)"


# G: one function builds "Cancel" from STR_CANCEL and styleLeaveButton(). The
# header's back arrow is buildSubHeader(s_scr, T(STR_PRL_TITLE), <handler>).
# The handlers (functions of the file named ...Cb) park a tap and nothing else:
# no widget made or freed in them, no search touched. printerListTick() ends a
# search first on back:
#   if (<back> && colourSearchBack(&s_find)) return;   (or the same in the
#   case of a switch, or "if (!colourSearchBack(&s_find)) { leave }")
# where <back> names the back action (the word "back" in a label or a flag), so
# that Cancel leaves at once, a word or not.
def check_g():
    find, _ = find_of()
    funcs = functions_of(PL)
    leave = [n for n, bodies in funcs.items()
             if any("STR_CANCEL" in b and re.search(NOT_ID + r"styleLeaveButton\(", b) for b in bodies)]
    assert leave, f"src/{PL}: no function builds a Cancel button (STR_CANCEL) in the style of the original (styleLeaveButton())"

    handlers = sorted(n for n in funcs if n.endswith("Cb"))
    assert len(handlers) >= 2, f"src/{PL}: fewer than two handlers (...Cb): the buttons and the back arrow park their taps"
    for n in handlers:
        for b in funcs[n]:
            assert not re.search(WIDGET_CALLS, b), \
                f"src/{PL}: {n}() makes or frees a widget: a handler parks, the tick builds"
            assert not re.search(r"colourSearch(?:Back|Tick|Close|Title)\(", b), \
                f"src/{PL}: {n}() works the search: a handler parks, the tick does"
    heads = [args for bodies in funcs.values() for b in bodies for _, args in calls_in(b, "buildSubHeader")]
    assert any(len(a) == 3 and "STR_PRL_TITLE" in a[1] and a[2] in handlers for a in heads), \
        f"src/{PL}: the header is not buildSubHeader(scr, T(STR_PRL_TITLE), <back handler>)"

    tick = inline_helpers(body_of(PL, "printerListTick"), funcs, set())
    call = "colourSearchBack(&" + find + ")"
    assert call in tick, f"src/{PL}: printerListTick() does not call {call}: back leaves the window with a word still set"
    ok = False
    for m in re.finditer(NOT_ID + r"if\(", tick):
        o = m.end() - 1
        c = close_of(tick, o)
        cond = tick[o + 1:c]
        if call not in cond:
            continue
        branch = tick[c + 1:close_of(tick, c + 1)] if tick[c + 1] == "{" else tick[c + 1:tick.find(";", c + 1) + 1]
        negated = "!" + call in cond
        assert negated or re.search(NOT_ID + r"(?:return|break)" + END_ID, branch), \
            f"src/{PL}: printerListTick(): when {call} ends a search the window must stay (return) and not go on to leave"
        labels = re.findall(r"case(\w+):", tick[:m.start()])
        around = [c.replace("colourSearchBack", "") for c in guards(tick, m.start())[:-1] + [cond]]   # the call's name says "back" too
        assert mentions(around, tick, r"(?i)back") or (labels and re.search(r"(?i)back", labels[-1])), \
            f"src/{PL}: printerListTick(): {call} is not on the back path only (Cancel must leave at once, a word or not)"
        ok = True
    assert ok, f"src/{PL}: printerListTick(): {call} is not asked in an if: its answer decides whether back leaves"


# ---------------------------------------------------------------------------
#  J: the texts (Task 3)
# ---------------------------------------------------------------------------

# J: lang.h has the eight ids; each has a row of three texts in lang.cpp (the
# German one as in the plan, a conversion where the table has one: %s in the
# line for no match, one %d in each kind of Klipper) and a row in the budgets.
def check_j():
    lang_h = (src / "lang.h").read_text(encoding="utf-8")
    lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
    budgets = (root / "tools" / "ui_budgets.tsv").read_text(encoding="utf-8")
    conv = r"%[-+ #0-9.]*[a-zA-Z]"
    texts = (
        ("STR_PRL_TITLE", "Drucker wählen", []),
        ("STR_PRL_FIND_KEY", "Drucker suchen", []),
        ("STR_PRL_FIND_NONE", 'Kein Drucker mit "%s"', ["%s"]),
        ("STR_PRL_MORE", "Nicht alle Drucker passen - mit der Lupe suchen", []),
        ("STR_PRL_KLIPPER", "Klipper", []),
        ("STR_PRL_TOOLS", "Klipper, %d Werkzeuge", ["%d"]),
        ("STR_PRL_MMU", "Klipper, MMU, %d Fächer", ["%d"]),
        ("STR_PRL_AFC", "Klipper, AFC, %d Fächer", ["%d"]),
    )
    for sid, german, conversions in texts:
        assert re.search(r"^\s*" + sid + r"\s*,", lang_h, re.M), f"src/lang.h: no {sid}"
        row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                        + sid + r"\b", lang_cpp)
        assert row, f"src/lang.cpp: no row of three texts for {sid}"
        de = row.group(1).replace('\\"', '"')
        assert de == german, f"src/lang.cpp: {sid} (de) is '{de}', the plan has '{german}'"
        for lang, text in zip(("de", "en", "fr"), row.groups()):
            assert text.strip(), f"src/lang.cpp: {sid} has no {lang} text"
            assert re.findall(conv, text) == conversions, \
                f"src/lang.cpp: {sid} ({lang}) has not {conversions or 'no conversion'}"
        assert re.search(r"^" + sid + r"\t\d+\t\d+\t(NEVER|DOT|WRAP:\d+)\t", budgets, re.M), \
            f"tools/ui_budgets.tsv: no budget for {sid}"


# ---------------------------------------------------------------------------
#  H, I: the AMS window (Task 4)
# ---------------------------------------------------------------------------

# H: ams_view.cpp has no nextPrinterCb and no STR_AMSV_PRN_OF (lang.h neither:
# the "n/m" text is gone). The callback of the chip is attached as
#   lv_obj_add_event_cb(s_printer_btn, <handler>, LV_EVENT_CLICKED, nullptr);
# and parks (s_list_pending = true), without making a widget, calling the
# window or touching s_printer_id, the memory or s_fetch_pending.
# handleAmsViewDeferredActions() calls printerListTick(), and opens the window
#   if (s_list_pending ...) { ...; showPrinterList(s_printers, s_printer_id, picked, left); }
# (s_printers is the pointer to the list in the PSRAM, beta.8). The text of the
# chip, written where the printer's label gets its text, holds LV_SYMBOL_DOWN
# (also for a message that keeps the chip: it is the same function, or a
# helper it calls). requestAmsView(), closeAmsView() and hideAmsViewOverlays()
# each call closePrinterList().
def check_h():
    av = code_of(AV)
    funcs = functions_of(AV)
    assert "nextPrinterCb" not in av, \
        f"src/{AV}: nextPrinterCb is still there: the chip must open the window, not step to the next printer"
    assert "STR_AMSV_PRN_OF" not in av, f"src/{AV}: STR_AMSV_PRN_OF is still used: the text 'n/m' is gone"
    assert "STR_AMSV_PRN_OF" not in cc.Source((src / "lang.h").read_text(encoding="utf-8")).code, \
        "src/lang.h: STR_AMSV_PRN_OF is still in the table (it goes with the 'n/m' text)"

    attached = re.findall(NOT_ID + r"lv_obj_add_event_cb\(s_printer_btn,(\w+),LV_EVENT_CLICKED,", av)
    assert len(attached) == 1, f"src/{AV}: the printer chip does not get exactly one click callback"
    cb = attached[0]
    cb_body = body_of(AV, cb)
    assert not re.search(WIDGET_CALLS, cb_body) and "showPrinterList(" not in cb_body, \
        f"src/{AV}: {cb}() builds or opens the window itself: the callback of the chip parks, the loop opens"
    assert not re.search(r"prefsPutInt\(|s_printer_id=|s_fetch_pending=true", cb_body), \
        f"src/{AV}: {cb}() chooses a printer or fetches: the chip only asks for the window"
    flags = re.findall(r"(?:^|[;{})])([A-Za-z_]\w*)=true;", cb_body)
    assert flags, f"src/{AV}: {cb}() parks nothing (no flag set to true)"
    flag = flags[0]

    deferred = body_of(AV, "handleAmsViewDeferredActions")
    assert calls_in(deferred, "printerListTick"), f"src/{AV}: handleAmsViewDeferredActions() does not call printerListTick()"
    opens = calls_in(deferred, "showPrinterList")
    assert len(opens) == 1, f"src/{AV}: handleAmsViewDeferredActions() does not call showPrinterList() once"
    at, args = opens[0]
    assert len(args) == 4 and args[0] == "s_printers" and args[1] == "s_printer_id", \
        f"src/{AV}: showPrinterList() is not called as showPrinterList(s_printers, s_printer_id, picked, left)"
    assert mentions(guards(deferred, at), deferred, NOT_ID + flag + END_ID), \
        f"src/{AV}: handleAmsViewDeferredActions() opens the window without the flag {flag} the chip parks"

    setters = [n for n, bodies in funcs.items() for b in bodies
               if any(a[0] == "s_printer_lbl" and a[1] != '""' for _, a in calls_in(b, "lv_label_set_text"))]
    assert setters, f"src/{AV}: nothing writes a text into the label of the printer chip"
    for n in set(setters):
        text = " ".join(inline_helpers("".join(funcs[n]), funcs, set()).split())
        assert "LV_SYMBOL_DOWN" in text, f"src/{AV}: {n}() writes the text of the chip without LV_SYMBOL_DOWN"

    for n in ("requestAmsView", "closeAmsView", "hideAmsViewOverlays"):
        assert calls_in(body_of(AV, n), "closePrinterList"), f"src/{AV}: {n}() does not call closePrinterList()"


# I: handleAmsViewDeferredActions() hides the AMS window before it opens the
# list (lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN) comes first), because
# tickPickCountdown() restarts the clock of "Cancel" while s_scr is hidden. The
# last two arguments of showPrinterList() are functions of the file:
#   left(): shows s_scr (lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN)), no s_fetch_pending
#   picked(int id): shows s_scr; if (id == s_printer_id) return; (before the
#     assignment, so the same printer asks for nothing); else
#     s_printer_id = id; prefsPutInt(AMSV_NVS_PRINTER, id); s_fetch_pending = true;
# The redraw of the weights reads
#   if (s_weights_redraw && !isAmsDetailPopupOpen() && !isPrinterListOpen())
def check_i():
    deferred = body_of(AV, "handleAmsViewDeferredActions")
    opens = calls_in(deferred, "showPrinterList")
    assert opens, f"src/{AV}: handleAmsViewDeferredActions() never calls showPrinterList()"
    at, args = opens[0]
    assert len(args) == 4, f"src/{AV}: showPrinterList() does not get four arguments"
    assert re.search(HIDDEN_ON, deferred[:at]), \
        f"src/{AV}: s_scr is not hidden before showPrinterList(): the countdown of Cancel would run under the window"
    tick = body_of(AV, "tickPickCountdown")
    assert "LV_OBJ_FLAG_HIDDEN" in tick and "restartPickCountdown()" in tick, \
        f"src/{AV}: tickPickCountdown() does not restart the clock while s_scr is hidden: the hidden window would not stop it"

    picked, left = args[2], args[3]
    for fn in (picked, left):
        assert re.fullmatch(r"\w+", fn), f"src/{AV}: showPrinterList() gets '{fn}' for a callback, not the name of a function"
    left_body = body_of(AV, left)
    assert re.search(HIDDEN_OFF, left_body), f"src/{AV}: {left}() does not show s_scr again"
    assert "s_fetch_pending" not in left_body, f"src/{AV}: {left}() fetches: leaving the window asks for nothing"

    ps = params_of(AV, picked)
    assert len(ps) == 1, f"src/{AV}: {picked}() does not take the printer id and nothing else"
    pid = ps[0]
    body = body_of(AV, picked)
    where = f"src/{AV}: {picked}()"
    same = rf"(?<![\w]){pid}(?:==|!=)s_printer_id(?![\w])|(?<![\w])s_printer_id(?:==|!=){pid}(?![\w])"
    compare = re.search(same, body)
    assign = re.search(NOT_ID + r"s_printer_id=" + pid + END_ID, body)
    assert compare and assign and compare.start() < assign.start(), \
        f"{where}: the id is not compared with s_printer_id before s_printer_id is set (the same printer must ask for nothing)"
    saved = calls_in(body, "prefsPutInt")
    assert any(len(a) == 2 and a[0] == "AMSV_NVS_PRINTER" and a[1] in (pid, "s_printer_id") for _, a in saved), \
        f"{where}: the chosen printer is not stored with prefsPutInt(AMSV_NVS_PRINTER, id)"
    fetch = [m.start() for m in re.finditer(NOT_ID + r"s_fetch_pending=true;", body)]
    assert len(fetch) == 1, f"{where}: s_fetch_pending = true is not set exactly once"
    assert mentions(guards(body, fetch[0]) + early_exits(body, fetch[0]), body, same), \
        f"{where}: the same printer fetches again (s_fetch_pending is set without the comparison of the id)"
    shows = [m.start() for m in re.finditer(HIDDEN_OFF, body)]
    assert shows, f"{where}: s_scr is not shown again"
    assert len(shows) > 1 or any(not mentions(guards(body, p) + early_exits(body, p), body, same) for p in shows), \
        f"{where}: s_scr is shown on the way of another printer only: the same printer would leave the window hidden"

    redraws = [m for m in re.finditer(NOT_ID + r"if\(", deferred)
               if "s_weights_redraw" in deferred[m.end():close_of(deferred, m.end() - 1)]]
    assert redraws, f"src/{AV}: the redraw of the weights (if (s_weights_redraw ...)) is gone"
    cond = deferred[redraws[-1].end():close_of(deferred, redraws[-1].end() - 1)]
    assert "!isAmsDetailPopupOpen()" in cond and "!isPrinterListOpen()" in cond, \
        f"src/{AV}: the weights are redrawn under the open list (the condition lacks !isPrinterListOpen())"


# ---------------------------------------------------------------------------
#  K: the chip stays (beta.8, held here)
# ---------------------------------------------------------------------------

# K: in fetchAndDraw() the net error, the fetch error and "no AMS" give the
# status row keep_chip = (s_printers->count > 1):
#   setStatus(T(STR_AMSV_ERR_NET), s_printers->count > 1);
#   setStatusFmt(STR_AMSV_ERR_HTTP, code, s_printers->count > 1);   (twice)
#   setStatus(T(STR_AMSV_NO_AMS), s_printers->count > 1);
# setStatus() and setStatusFmt() pass keep_chip on to statusRowPlain(), which
# hides the chip (lv_obj_add_flag(s_printer_btn, LV_OBJ_FLAG_HIDDEN)) only
# without it: after an if (keep_chip && ...) { ...; return; } or under !keep_chip.
def check_k():
    body = body_of(AV, "fetchAndDraw")
    keep = r"\(*s_printers->count\)?>1\)*"
    for fn, sid, index, keep_at, times in (("setStatus", "STR_AMSV_ERR_NET", 0, 1, 1),
                                           ("setStatusFmt", "STR_AMSV_ERR_HTTP", 0, 2, 2),
                                           ("setStatus", "STR_AMSV_NO_AMS", 0, 1, 1)):
        calls = [a for _, a in calls_in(body, fn) if sid in a[index]]
        assert len(calls) == times, f"src/{AV}: fetchAndDraw() does not show {sid} {times} time(s) with {fn}()"
        for a in calls:
            assert len(a) == keep_at + 1 and re.fullmatch(keep, a[keep_at]), \
                f"src/{AV}: fetchAndDraw(): {fn}({sid}, ...) does not keep the chip (s_printers->count > 1 as keep_chip)"
    for fn in ("setStatus", "setStatusFmt"):
        assert "statusRowPlain(keep_chip)" in body_of(AV, fn), f"src/{AV}: {fn}() does not pass keep_chip on to statusRowPlain()"
    plain = body_of(AV, "statusRowPlain")
    hide = re.search(NOT_ID + r"lv_obj_add_flag\(s_printer_btn,LV_OBJ_FLAG_HIDDEN\)", plain)
    assert hide, f"src/{AV}: statusRowPlain() never hides the chip (a loading page has none)"
    left_early = False
    for m in re.finditer(NOT_ID + r"if\(", plain[:hide.start()]):
        c = close_of(plain, m.end() - 1)
        if re.search(NOT_ID + r"keep_chip" + END_ID, plain[m.end():c]) and plain[c + 1] == "{":
            e = close_of(plain, c + 1)
            left_early = left_early or (e < hide.start() and re.search(NOT_ID + r"return" + END_ID, plain[c:e]) is not None)
    assert left_early or mentions(guards(plain, hide.start()), plain, r"!keep_chip"), \
        f"src/{AV}: statusRowPlain() hides the chip also when keep_chip is set"


# ---------------------------------------------------------------------------
#  L: the two tests run in the house-rules job of the CI (behind 'Test AMS Klipper')
# ---------------------------------------------------------------------------

# L: both steps are there with their run: lines, "Test printer pick rules"
# before "Test printer list" (the rule before the window that uses it), both
# somewhere behind "Test AMS Klipper". Other steps may stand between them.
def check_l():
    ci = (root / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    steps = re.findall(r"- name: (Test [^\n]*)\n\s+run: ([^\n]*)", ci)
    names = [n for n, _ in steps]
    for name, script in (("Test printer pick rules", "test_printer_pick_rules.py"), ("Test printer list", "test_printer_list.py")):
        assert name in names, f".github/workflows/ci.yml: the step '{name}' is not there"
        assert dict(steps)[name].strip() == f"python3 test/{script}", \
            f".github/workflows/ci.yml: the step '{name}' does not run python3 test/{script}"
    assert "Test AMS Klipper" in names, ".github/workflows/ci.yml: the step 'Test AMS Klipper' is not there"
    klipper, rules, window = (names.index(n) for n in ("Test AMS Klipper", "Test printer pick rules", "Test printer list"))
    assert klipper < rules < window, \
        ".github/workflows/ci.yml: 'Test printer pick rules' and then 'Test printer list' do not come after 'Test AMS Klipper'"


CHECKS = (check_a, check_b, check_c, check_d, check_e, check_f, check_g, check_j, check_h, check_i, check_k, check_l)

if __name__ == "__main__":
    for check in CHECKS:
        check()
    print("printer list: ok")
