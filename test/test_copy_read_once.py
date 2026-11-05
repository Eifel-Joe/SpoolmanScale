"""Read the source tree without a compiler and check that a copy behind BamBuddy on Spoolman reads its template once (Punkteliste n43, v0.8.7-beta.5, Entwurf E3). A row out of the list cache and a template typed by its number were read through the mapped spool, and the card read the same spool again, raw, with its profiles and its empty weight: up to about 12 s without a card. Now one read serves both: copyTemplateRead() loads the template the card needs (copyBbsLoad()) and hands its mapped form to the row, and the card takes that fresh load (copyBbsTakeFresh()) instead of reading again. Every other server reads as before."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

COPY = "ui/copy_flow.cpp"
FLOW = "ui/spool_flow.cpp"
CARD = "ui/copy_confirm_popup.cpp"
BBS = "services/copy_bbs.cpp"
BB_API = "services/bambuddy_api.cpp"


def squeeze(text):
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    source = cc.Source((src / rel).read_text(encoding="utf-8"))
    spans = [(start, end) for found, start, end in cc.find_functions(source.code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    # Comments blanked, string literals kept (code_str is aligned with code).
    return squeeze(source.code_str[spans[0][0] + 1:spans[0][1]])


# 1. One read for the template: behind BamBuddy on Spoolman the card's load,
# mapped for the row; elsewhere the mapped read as before.
read = body_of(CARD, "copyTemplateRead")
bbs = read.find("spoolTypesActive()")
assert bbs >= 0, f"src/{CARD}: copyTemplateRead() does not tell BamBuddy on Spoolman apart"
assert "copyBbsLoad(" in read and "copyBbsMapped(" in read, \
    f"src/{CARD}: copyTemplateRead() does not load the template the card needs and map it"
assert read.find("copyBbsLoad(") < read.find("copyBbsMapped("), \
    f"src/{CARD}: copyTemplateRead() maps before it loads"
assert "backendGetSpoolJson(" in read, f"src/{CARD}: copyTemplateRead() lost the read for the other servers"

# 2. Both ways that read a template before the card go through it, and none
# of them reads the spool by itself any more.
for rel, name in ((COPY, "copyRowRefresh"), (FLOW, "copyIdLookup")):
    body = body_of(rel, name)
    assert "copyTemplateRead(" in body, f"src/{rel}: {name}() does not read through copyTemplateRead()"
    assert "backendGetSpoolJson(" not in body, f"src/{rel}: {name}() reads the template a second way"

# The picker's row out of the cache (an NTAG copy) is the third way: in copy
# mode it reads through copyTemplateRead() as well, a link reads as before.
row = body_of(FLOW, "linkRefreshRow")
assert "copy_flow_via_list?copyTemplateRead(" in row, \
    f"src/{FLOW}: linkRefreshRow() reads a copy's template a second way"

# 3. The card takes a fresh load of the same template instead of reading it
# again; any other card reads as before.
card = body_of(CARD, "showCopyConfirmPopup")
fresh = card.find("copyBbsTakeFresh(template_spool_id)")
load = card.find("copyBbsLoad(template_spool_id)")
assert fresh >= 0, f"src/{CARD}: showCopyConfirmPopup() never takes a fresh load"
assert 0 <= fresh < load, f"src/{CARD}: showCopyConfirmPopup() loads before it looks for a fresh load"

# 4. A load is fresh once, for the card right after it, and only for its own
# template; clearing it ends that.
take = body_of(BBS, "copyBbsTakeFresh")
assert "s_loaded_id==template_spool_id" in take and "s_fresh=false" in take, \
    f"src/{BBS}: copyBbsTakeFresh() is not one-shot for its own template"
assert "s_fresh=true" in body_of(BBS, "copyBbsLoad"), f"src/{BBS}: copyBbsLoad() does not mark its load fresh"
assert "s_fresh=false" in body_of(BBS, "copyBbsClear"), f"src/{BBS}: copyBbsClear() leaves a load fresh"
mapped = body_of(BBS, "copyBbsMapped")
assert "bbMapSpool(" in mapped, f"src/{BBS}: copyBbsMapped() does not map like the mapped read"
# The mapped read and copyBbsMapped() map alike: one mapping.
assert "mapSpool(" in body_of(BB_API, "bbMapSpool"), f"src/{BB_API}: bbMapSpool() is not mapSpool()"

print("copy reads its template once: ok")
