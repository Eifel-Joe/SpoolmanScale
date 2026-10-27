"""Read the source tree without a compiler and check who may open the list of spool types behind Bambuddy without Spoolman (V33): the catalog is Bambuddy's own table in both inventory modes, but only the card for a new spool from a Bambu tag asks for the list there. Every other caller builds its request with any_inventory false and stays on Spoolman mode, spoolTypesCreate() stays on Spoolman mode, the card asks before it is built from a tag and not at all when it is built for a database entry, and its question takes the reading the card takes (startGross()), so a scale that is not fitted or not ready asks nothing."""
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

SOURCE_SUFFIXES = (".h", ".cpp", ".hpp", ".c", ".ino")
ANY_INVENTORY_TRUE = re.compile(r"\bany_inventory\s*=\s*true\b")
POPUP = "ui/tag_create_popup.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def code_of(rel):
    """The code of one file under src/ (comments and string contents blanked)."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8")).code


def body_of(rel, name):
    """The code between the braces of the one function called name."""
    code = code_of(rel)
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


# 1. One place asks the list for Bambuddy in either inventory mode, and it is
# the card for a tag. A second one would open the list for a flow that has
# no catalog to read without Spoolman (copy, database, first weighing).
sites = []
for path in sorted(src.rglob("*")):
    if path.suffix not in SOURCE_SUFFIXES:
        continue
    source = cc.Source(path.read_text(encoding="utf-8"))
    rel = path.relative_to(src).as_posix()
    for match in ANY_INVENTORY_TRUE.finditer(source.code):
        sites.append(f"src/{rel}:{source.line_of(match.start())}")
assert len(sites) == 1, \
    f"any_inventory is set to true in {len(sites)} places, expected exactly one: {sites}"
assert sites[0].startswith(f"src/{POPUP}:"), \
    f"any_inventory is set to true outside the card for a tag ({POPUP}): {sites[0]}"

# 2. The list opens on that wish: the wish and the wider check are in the one
# place that decides whether the list opens at all.
body = body_of("ui/spool_type_list.cpp", "showSpoolTypeList")
assert "any_inventory" in body, \
    "src/ui/spool_type_list.cpp: showSpoolTypeList() does not read req.any_inventory"
assert "spoolTypesCatalogActive()" in body, \
    "src/ui/spool_type_list.cpp: showSpoolTypeList() does not ask spoolTypesCatalogActive()"
# The two checks above would also pass for a swapped ternary (the wide check
# for the other callers, the narrow one for the tag's card), so the pairing is
# pinned as one expression.
assert squeeze("req.any_inventory ? spoolTypesCatalogActive() : spoolTypesActive()") in body, \
    "src/ui/spool_type_list.cpp: showSpoolTypeList() does not pair any_inventory with spoolTypesCatalogActive() and the rest with spoolTypesActive(), the ternary is swapped or changed"

# 3. Reading the catalog follows Bambuddy in both modes; adding to it stays
# with Spoolman mode, the one place where the scale writes a type.
body = body_of("services/spool_types.cpp", "spoolTypesCreate")
assert "spoolTypesActive()" in body, \
    "src/services/spool_types.cpp: spoolTypesCreate() no longer asks spoolTypesActive()"
assert "spoolTypesCatalogActive()" not in body, \
    "src/services/spool_types.cpp: spoolTypesCreate() asks spoolTypesCatalogActive(), a new type is for Spoolman mode only"
body = body_of("services/spool_types.cpp", "spoolTypesLoad")
assert "spoolTypesCatalogActive()" in body, \
    "src/services/spool_types.cpp: spoolTypesLoad() does not ask spoolTypesCatalogActive()"
assert "spoolTypesActive()" not in body, \
    "src/services/spool_types.cpp: spoolTypesLoad() still asks spoolTypesActive(), the list would not load without Spoolman"

# 4. The card asks before it is built from a tag, and not when it is built for
# a database entry or a sort typed in: those ask for themselves (or not at all).
body = body_of(POPUP, "showTagCreatePopup")
ask, build = body.find("tagTypeAsk()"), body.find("buildCard()")
assert ask >= 0, f"src/{POPUP}: showTagCreatePopup() does not call tagTypeAsk()"
assert build >= 0, f"src/{POPUP}: showTagCreatePopup() does not call buildCard()"
assert ask < build, f"src/{POPUP}: showTagCreatePopup() calls tagTypeAsk() after buildCard()"
# Asking first is not enough: the card must not be built a second time when the
# list was opened (the list builds it on the answer), so the answer of
# tagTypeAsk() ends the function. A bare tagTypeAsk(); buildCard(); fails here.
assert squeeze("if (tagTypeAsk()) return;") in body, \
    f"src/{POPUP}: showTagCreatePopup() does not return when tagTypeAsk() opened the list, the card would be built twice"
body = body_of(POPUP, "showTagCreatePopupFor")
assert "tagTypeAsk" not in body, \
    f"src/{POPUP}: showTagCreatePopupFor() calls tagTypeAsk(), a database entry asks for itself"

# 5. The question is the rule of spool_type_rules.h on the card's own facts:
# the tag, the backend, the reading the card takes (startGross(), which is 0
# for a scale that is not fitted or not ready) and the type asked so far.
# The request carries no measured weight: that would add "take measured" and
# "new type" to the list (V33).
body = body_of(POPUP, "tagTypeAsk")
for needle in ("spoolTypeTagAsk(", "tagIsBambu(s_in.link_id)", "backendIsBamBuddy()", "startGross()"):
    assert squeeze(needle) in body, f"src/{POPUP}: tagTypeAsk() does not use {needle}"
assert "measured_g" not in body, \
    f"src/{POPUP}: tagTypeAsk() sets measured_g, the list would offer to take the measured weight or add a type"

print("spool type gate: ok")
