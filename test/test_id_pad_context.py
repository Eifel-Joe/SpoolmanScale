"""Read the source tree without a compiler and check what the number pad for a spool id says about the tag (Punkteliste n16): a link always joins the tag on the pad, so its pad names that tag's UID even when a cancelled "create spool" left the flag for a create without a tag on. Only a copy reads the flag, and there "no tag - not weighed" is what the choice decided."""
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


pad = body_of(FLOW, "showIdInputPopup")
# The three lines of the context, in their order: Bambu, the flag (copy only),
# the UID.
assert squeeze("if (is_bambu)") in pad, f"src/{FLOW}: showIdInputPopup() lost its Bambu line"
assert squeeze("else if (is_copy && noTagMode()) copyT(ctx, sizeof(ctx), STR_NOTAG_CTX);") in pad, \
    f"src/{FLOW}: showIdInputPopup() reads the no-tag flag for a link, not only for a copy"
assert pad.count("noTagMode()") == 1, \
    f"src/{FLOW}: showIdInputPopup() reads the no-tag flag more than once"
# (String contents are blanked by the tokenizer: the UID line is told by its argument.)
assert squeeze('else snprintf(ctx, sizeof(ctx), "", link_tag_uid);') in pad, \
    f"src/{FLOW}: showIdInputPopup() no longer names the tag's UID otherwise"

print("id pad names the tag of a link: ok")
