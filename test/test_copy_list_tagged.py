"""Read the source tree without a compiler and check that the copy lists keep the spools that already carry a tag (Punkteliste n14): the fetch filter keeps them in every copy through the picker, active or archived, and the pass that renders the rows must not drop them again, whatever the append switch says. Before, only the archived list kept them, and the active list of a copy lost exactly the template that is bought again."""
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

FLOW = "ui/spool_flow.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    code = cc.Source(path.read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


# 1. The fetch keeps a tagged spool in every copy through the picker: the
# test for a tag stands behind !copy_flow_via_list.
fetch = body_of(FLOW, "linkFilterVerdict")
assert squeeze("!copy_flow_via_list&&!link_cu_ok&&spoolHasAnyTag(spool)") in fetch, \
    f"src/{FLOW}: linkFilterVerdict() no longer keeps tagged spools in a copy through the picker"

# 2. The rows: a copy through the picker never skips a row, active or
# archived, and that is the first thing the function says.
skip = body_of(FLOW, "linkSpoolSkip")
assert skip.startswith(squeeze("if (copy_flow_via_list) return false;")), \
    f"src/{FLOW}: linkSpoolSkip() does not keep every row of a copy through the picker first"
assert "copy_flow_archived" not in skip, \
    f"src/{FLOW}: linkSpoolSkip() still keeps tagged rows for the archived copy list only"

print("copy list keeps tagged spools: ok")
