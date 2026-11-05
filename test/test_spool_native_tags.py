"""Read the source tree without a compiler and check that the link list counts Spoolman's own tag relation (0.27) as bound (f11, part of renatobrunella's patch sdfp-v0.8.3-1, v0.8.7-beta.5): spoolHasAnyTag() reads tags[].uid as well as the extra fields, the list fetch asks the server for that relation, and the list cache marks a spool bound after every link, from the scale and from outside, as a fresh list would say."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

FLOW = "ui/spool_flow.cpp"
LINK = "services/tag_link.cpp"


def squeeze(text):
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    code = cc.Source((src / rel).read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


# 1. The relation counts, and before the extra fields: a spool without extra
# fields can be bound through it.
has = body_of(FLOW, "spoolHasAnyTag")
native = has.find('spool["tags"]')
assert native >= 0, f"src/{FLOW}: spoolHasAnyTag() does not read Spoolman's own tag relation"
assert '["uid"]' in has, f"src/{FLOW}: spoolHasAnyTag() does not read the uid of a tag"
assert native < has.find("extra.isNull()"), \
    f"src/{FLOW}: spoolHasAnyTag() returns for a spool without extra fields before it reads the relation"

# 2. The list fetch asks for the relation, or a filtered list has none of it.
code = squeeze(cc.Source((src / FLOW).read_text(encoding="utf-8")).code)
assert 'fL["tags"][0]["uid"]=true' in code, f"src/{FLOW}: the link list's filter drops the tag relation"

# 3. After a link the cache says what a fresh list would say, whichever field
# was written: no exception for the relation any more, here and for a link
# that came from outside (tag_link.cpp).
for rel in (FLOW, LINK):
    text = squeeze(cc.Source((src / rel).read_text(encoding="utf-8")).code)
    assert "spoolCacheSetBound(" in text, f"src/{rel}: spoolCacheSetBound() is gone"
    assert "if(!tagFieldSelected().is_native)spoolCacheSetBound(" not in text, \
        f"src/{rel}: a link through Spoolman's own relation still leaves the spool free in the cache"

print("native tag relation in the link list: ok")
