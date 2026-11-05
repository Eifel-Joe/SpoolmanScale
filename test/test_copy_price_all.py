"""Read the source tree without a compiler and check the price of a copy on every server (o19, part of Niko11111 c3a1ec3, with Entwurf E2 A of v0.8.7-beta.5): the copy card has the price button on every server and starts it from the template's price in the server's own unit (per kg with BamBuddy, with and without Spoolman; per spool with Spoolman direct and FilaMan). Every row the card can start from carries that price, the translations of BamBuddy and FilaMan bring it along, and the Spoolman list asks for it. BamBuddy takes the price with the create; Spoolman and FilaMan get it right after, one spool or several (V44), and a price that does not arrive leaves the spool and says "Spool created, price not saved"."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

CARD = "ui/copy_confirm_popup.cpp"
COPY = "ui/copy_flow.cpp"
FLOW = "ui/spool_flow.cpp"
BATCH = "ui/batch_create.cpp"
BBS = "services/copy_bbs.cpp"
BACKEND = "services/backend_api.cpp"
BB_API = "services/bambuddy_api.cpp"
FM_API = "services/filaman_api.cpp"


def squeeze(text):
    return re.sub(r"\s+", "", text)


def code_of(rel):
    """Comments blanked, string literals kept."""
    return cc.Source((src / rel).read_text(encoding="utf-8")).code_str


def functions_of(rel):
    source = cc.Source((src / rel).read_text(encoding="utf-8"))
    return [(name, squeeze(source.code_str[start + 1:end]))
            for name, start, end in cc.find_functions(source.code)]


def body_of(rel, name):
    found = [body for n, body in functions_of(rel) if n == name]
    assert len(found) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return found[0]


def click_handler_of(rel, marker):
    source = cc.Source((src / rel).read_text(encoding="utf-8"))
    found = [squeeze(source.code_str[b0 + 1:b1]) for _, _, b0, b1 in cc.handlers(source)
             if squeeze(marker) in squeeze(source.code_str[b0 + 1:b1])]
    assert len(found) == 1, f"src/{rel}: {len(found)} event handlers contain {marker}, expected one"
    return found[0]


# 1. Rows and looks carry the template's price.
internal = squeeze(code_of("ui/spool_flow_internal.h"))
row = internal[internal.find("structUnlinkedSpool{"):]
row = row[:row.find("};")]
assert "floatprice;" in row, "src/ui/spool_flow_internal.h: UnlinkedSpool has no price"
look = internal[internal.find("structCopyLook{"):]
look = look[:look.find("};")]
assert "floatprice;" in look, "src/ui/spool_flow_internal.h: CopyLook has no price"
assert "look.price=s.price" in body_of(FLOW, "copyLookFromRow"), \
    f"src/{FLOW}: copyLookFromRow() leaves the price behind"
# One rule for what the template cost, in the unit of the server.
price_of = body_of(FLOW, "spoolTemplatePrice")
assert '"cost_per_kg"' in price_of and '"price"' in price_of and "backendIsBamBuddy()" in price_of, \
    f"src/{FLOW}: spoolTemplatePrice() does not read cost_per_kg with BamBuddy and price otherwise"
# Every place that fills a row from a spool fills its price too.
for rel in (FLOW, COPY):
    for name, body in functions_of(rel):
        if re.search(r"\b\w+\.spool_weight=", body):
            assert ".price=spoolTemplatePrice(" in body, \
                f"src/{rel}: {name}() fills a row without the template's price"
# The copy by its number builds its look from the spool it read.
by_id = body_of(FLOW, "copyIdLookup")
assert "look.price=spoolTemplatePrice(" in by_id, \
    f"src/{FLOW}: the copy by number starts the card without the template's price"

# 2. The servers bring the price along: BamBuddy's per kg, FilaMan's per spool
# (purchase_price, also in the list filter), and the Spoolman list asks for it.
assert 'dst["cost_per_kg"]' in body_of(BB_API, "mapSpool"), \
    f"src/{BB_API}: mapSpool() drops BamBuddy's price per kg"
assert 'src["purchase_price"]' in body_of(FM_API, "mapSpool") and 'dst["price"]' in body_of(FM_API, "mapSpool"), \
    f"src/{FM_API}: mapSpool() drops FilaMan's price"
assert '"purchase_price"' in body_of(FM_API, "spoolListFilter"), \
    f"src/{FM_API}: the FilaMan list filter drops the price that mapSpool() reads"
assert 'fL["price"]=true' in squeeze(code_of(FLOW)), f"src/{FLOW}: the link list's filter drops the price"

# 3. The card: the price button on every server, per kg only with BamBuddy.
card = body_of(CARD, "buildCopyCard")
assert "buildCopyPriceButton(box,row_y)" in card, f"src/{CARD}: the card has no price button"
assert "if(s_copy_bbs)buildCopyPriceButton(" not in card, \
    f"src/{CARD}: the price button is still for BamBuddy on Spoolman only"
assert "constintans_w=COPY_CARD_ANSWER_W;" in card, \
    f"src/{CARD}: the answers do not make room for the price button on every server"
assert "s_copy_price_tpl=copy_template_look.price" in squeeze(code_of(CARD)), \
    f"src/{CARD}: outside BamBuddy on Spoolman the button does not start from the template's price"
refresh = body_of(CARD, "copyPriceRefresh")
assert "copyPricePerKg()" in refresh, f"src/{CARD}: the button says /kg whatever the server"
pad = click_handler_of(CARD, "BTN: CopyConfirm -> Price")
assert "copyPricePerKg()" in pad and "showPricePadTitled(" in pad and "showPricePad(" in pad, \
    f"src/{CARD}: the pad asks per kg whatever the server"
per_kg = body_of(CARD, "copyPricePerKg")
assert "backendIsBamBuddy()" in per_kg, f"src/{CARD}: copyPricePerKg() is not BamBuddy's"
typed = body_of(CARD, "copyPriceTyped")
assert "!s_copy_bbs" not in typed, f"src/{CARD}: a price typed outside BamBuddy on Spoolman is dropped"
assert "copyBbsSetPrice(price)" in typed, f"src/{CARD}: a typed price does not reach the BamBuddy create"
assert "copyCardPrice()" in body_of(CARD, "copyPriceAfterCreate"), \
    f"src/{CARD}: the price after the create is not the card's"

# 4. BamBuddy without Spoolman takes the price with the create; a refused one
# is a part not taken over.
before = body_of(BBS, "createAsBefore")
assert "copyRulesCostPerKg(" in before and "ns.cost_per_kg" in before, \
    f"src/{BBS}: a copy in BamBuddy's own inventory goes without a price"
assert "CM_PRICE" in before, f"src/{BBS}: a refused price in BamBuddy's own inventory is not reported"
create = body_of(COPY, "doCopySpoolCreate")
assert "backendIsBamBuddy()?copyBbsLastMissing()" in create, \
    f"src/{COPY}: what a copy in BamBuddy's own inventory did not take over is not read"

# 5. Spoolman and FilaMan: the price right after the create, for one copy and
# for several, and the status line says when it did not arrive.
assert "backendPatchSpoolPrice(" in body_of(CARD, "copyPriceAfterCreate"), \
    f"src/{CARD}: copyPriceAfterCreate() does not write the price"
assert "backendIsBamBuddy()" in body_of(CARD, "copyPriceAfterCreate"), \
    f"src/{CARD}: copyPriceAfterCreate() writes a second price for BamBuddy, whose create took it"
patch = body_of(BACKEND, "backendPatchSpoolPrice")
assert "spoolmanPatchSpoolPrice(" in patch and "filamanPatchSpoolPrice(" in patch, \
    f"src/{BACKEND}: backendPatchSpoolPrice() does not cover Spoolman and FilaMan"
for rel, name in ((COPY, "doCopySpoolCreate"), (BATCH, "batchCopyCreate")):
    body = body_of(rel, name)
    assert "if(copyPriceAfterCreate(new_id))missing|=CM_PRICE;" in body, \
        f"src/{rel}: {name}() does not write the price of the copy, or drops a lost one"
status = body_of(COPY, "copyCreatedStatus")
lost = status.find("STR_TAGNEW_PRICE_LOST")
assert lost >= 0 and "missing==CM_PRICE&&!backendIsBamBuddy()" in status, \
    f"src/{COPY}: copyCreatedStatus() does not say 'Spool created, price not saved' for a lost price"

print("copy price on every server: ok")
