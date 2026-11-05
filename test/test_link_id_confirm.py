"""Read the source tree without a compiler and check the link by a typed spool number (o06, Niko11111 6eff48d and 1156587, v0.8.7-beta.5): a number that was found asks the question a tap on the list asks (the spool with its colour, name and weight, then "Link") before anything is written, and an answer the scale could not read (-2) is said as such before the catch-all "not found" for every other negative code."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

FLOW = "ui/spool_flow.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    """The code between the braces of the one function called name, comments blanked."""
    source = cc.Source((src / rel).read_text(encoding="utf-8"))
    spans = [(start, end) for found, start, end in cc.find_functions(source.code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    # Comments blanked, string literals kept (code_str is aligned with code).
    return squeeze(source.code_str[spans[0][0] + 1:spans[0][1]])


body = body_of(FLOW, "linkIdLookupAndPatch")

# 1. An unreadable answer first: -2 is a spool the server did send, and the
# catch-all for negative codes said "not found" for it (1156587).
unreadable = body.find("if(code==-2)")
not_found = body.find("if(code==404||code<0)")
assert unreadable >= 0, f"src/{FLOW}: linkIdLookupAndPatch() has no own branch for code -2"
assert not_found >= 0, f"src/{FLOW}: linkIdLookupAndPatch() has no 'not found' branch"
assert unreadable < not_found, \
    f"src/{FLOW}: linkIdLookupAndPatch() says 'not found' before it looks for an unreadable answer (-2)"

# 2. A found spool that is neither bound nor different from the tag gets the
# list's question before the link: its row is looked up and the confirmation
# opened for it, with the tag kind of this link (6eff48d).
ask = body.find("showLinkConfirmPopup(i)")
assert ask >= 0, f"src/{FLOW}: linkIdLookupAndPatch() never asks before it links a typed number"
assert "link_flow_is_bambu=is_bambu" in body, \
    f"src/{FLOW}: linkIdLookupAndPatch() opens the question without the tag kind of this link"
last_link = body.rfind("doLinkPatch(entered_id,is_bambu)")
assert last_link > ask, \
    f"src/{FLOW}: linkIdLookupAndPatch() links a typed number without the question first"
# The question comes after the warnings about a bound spool or a mismatching
# tag, which already ask in their own words.
assert body.find("showWarnPopupA(") < ask, \
    f"src/{FLOW}: the question comes before the warning about a bound spool"

# 3. The question shows the colour of the spool (6eff48d): the swatch is
# painted from the row's colour.
popup = body_of(FLOW, "showLinkConfirmPopup")
assert "swatchPaintHex(sw,s.color_hex)" in popup, \
    f"src/{FLOW}: the link question does not show the spool's colour"

print("link by id asks first: ok")
