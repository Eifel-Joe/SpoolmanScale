"""Read the source tree without a compiler and check that our cancel buttons look like every other way out of the original (n17, V43): the choice of how to give the tare and the chooser of spool parts hand their Cancel button to styleLeaveButton() once its caption is set, as db_pick_screen.cpp does; the list of spool types gives its skip button that look exactly when it says Cancel, and keeps the secondary fill for any other caption ("Skip", "Type in")."""
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

TARE = "ui/tare_entry.cpp"
EXTRAS = "ui/extras_chooser.cpp"
STL = "ui/spool_type_list.cpp"
NOT_ID = r"(?<![A-Za-z0-9_])"
END_ID = r"(?![A-Za-z0-9_])"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    """The code between the braces of the one function called name, squeezed."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    code = cc.Source(path.read_text(encoding="utf-8")).code
    bodies = [code[start + 1:end] for found, start, end in cc.find_functions(code) if found == name]
    assert len(bodies) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(bodies[0])


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


def statement_at(body, pos):
    """The statement around pos, from the end of the one before it to its ';'."""
    start = max(body.rfind(";", 0, pos), body.rfind("{", 0, pos), body.rfind("}", 0, pos)) + 1
    return start, body.find(";", pos)


# 1. A Cancel of our own: the button whose caption is T(STR_CANCEL) goes to
# styleLeaveButton() after the caption is on it (the style reads the label
# to put its icon in front), wherever the caption is set: padLabel(button,
# ...), or a label created on the button.
for rel, fn in ((TARE, "showTareChoice"), (EXTRAS, "showExtrasChooser")):
    where = f"src/{rel}: {fn}()"
    body = body_of(rel, fn)
    hits = [m.start() for m in re.finditer(r"T\(STR_CANCEL\)", body)]
    assert hits, f"{where}: no button says T(STR_CANCEL)"
    for at in hits:
        start, end = statement_at(body, at)
        stmt = body[start:end]
        if stmt.startswith("styleLeaveButton("):
            continue   # the button is made inside the call
        label = re.match(r"lv_label_set_text\((\w+),", stmt)
        if label:
            made = re.search(NOT_ID + label.group(1) + r"=lv_label_create\((\w+)\)", body[:start])
            assert made, f"{where}: the label {label.group(1)} with T(STR_CANCEL) is on no button of this function"
            button = made.group(1)
        else:
            first = re.match(r"\w+\((\w+),", stmt)
            assert first, f"{where}: T(STR_CANCEL) is not the caption of a button ('{stmt}')"
            button = first.group(1)
        assert re.search(r"styleLeaveButton\(" + button + r"[,)]", body[end:]), \
            f"{where}: the Cancel button {button} is not handed to styleLeaveButton() after its caption is set"

# 2. The list of spool types: its skip button says what the caller asked for
# (s_req.skip_text). It leaves in the original's style when that is Cancel,
# and only then; any other caption keeps the secondary fill.
where = f"src/{STL}: stlButton()"
body = body_of(STL, "stlButton")
made = re.search(r"lv_obj_t\*(\w+)=lv_btn_create\(", body)
assert made, f"{where}: no button"
button = made.group(1)
leaves = [m.start() for m in re.finditer(r"styleLeaveButton\(", body)]
assert leaves, f"{where}: the skip button never takes the leave style for Cancel"
caption = body.find("lv_label_set_text(")
for at in leaves:
    assert re.match(r"styleLeaveButton\(" + button + r"[,)]", body[at:]), f"{where}: styleLeaveButton() is not given {button}"
    assert 0 <= caption < at, f"{where}: styleLeaveButton() comes before the caption is set"
    conds = guards(body, at)
    assert mentions(conds, body, r"STL_ACT_SKIP"), f"{where}: styleLeaveButton() is not for the skip button alone"
    assert mentions(conds, body, r"STR_CANCEL"), \
        f"{where}: styleLeaveButton() is not for the caption Cancel alone (s_req.skip_text == STR_CANCEL)"
fills = [m.start() for m in re.finditer(r"uiSecondaryFill\(", body)]
assert fills, f"{where}: a skip button that does not say Cancel lost its secondary fill"
for at in fills:
    assert mentions(guards(body, at), body, r"STL_ACT_SKIP"), f"{where}: uiSecondaryFill() is not the skip button's"

print("leave buttons: ok")
