"""Read the source tree without a compiler and check the typical density on the card for a new filament (o04, Niko11111 b67f5ab and e1588eb, built into our density field of V34; v0.8.7-beta.5, Entwurf E1 A). When no source knows a density and the material has a typical value, the status line names it, the left button reads "Use" and takes it as if it were typed, and the middle button (the price's) reads "Change" and opens our density field with the value filled in. Any other status ends the offer and gives the price button back. Without a typical value the card asks as before (test_density_card.py). And the product name finds the density of a Bambu support material in the light form of the database through its base material."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

POPUP = "ui/tag_create_popup.cpp"
PLANNER = "services/spoolman_filament.cpp"


def squeeze(text):
    return re.sub(r"\s+", "", text)


def source_of(rel):
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def body_of(rel, name):
    source = source_of(rel)
    spans = [(start, end) for found, start, end in cc.find_functions(source.code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    # Comments blanked, string literals kept (code_str is aligned with code).
    return squeeze(source.code_str[spans[0][0] + 1:spans[0][1]])


def click_handler_of(rel, marker):
    source = source_of(rel)
    found = [squeeze(source.code_str[b0 + 1:b1]) for _, _, b0, b1 in cc.handlers(source)
             if squeeze(marker) in squeeze(source.code_str[b0 + 1:b1])]
    assert len(found) == 1, f"src/{rel}: {len(found)} event handlers contain {marker}, expected one"
    return found[0]


# 0. The texts, in the table.
lang_h = (src / "lang.h").read_text(encoding="utf-8")
for sid in ("STR_DENSITY_TYPICAL", "STR_DENSITY_TAKE", "STR_DENSITY_CHANGE"):
    assert re.search(r"\b" + sid + r"\b", lang_h), f"src/lang.h: {sid} is missing"

# 1. The offer: the typical value of the material, on the status line, with
# "Use" on the left button and "Change" on the price button.
offer = body_of(POPUP, "showDensityOffer")
for needle in ("densityTypical(s_in.material)", "STR_DENSITY_TYPICAL", "s_density_offer=true",
               "setOkText(STR_DENSITY_TAKE)", "refreshPrice()"):
    assert squeeze(needle) in offer, f"src/{POPUP}: showDensityOffer() does not use {needle}"
assert offer.find("showStatus(") < offer.find("s_density_offer=true"), \
    f"src/{POPUP}: showDensityOffer() sets the offer before showStatus(), which ends it"

# 2. Every other status ends the offer and gives the price button back.
status = body_of(POPUP, "showStatus")
assert "s_density_offer=false" in status, f"src/{POPUP}: showStatus() does not end the offer"
assert "refreshPrice()" in status, f"src/{POPUP}: showStatus() leaves the price button saying 'Change'"

# 3. The plan without a density offers the typical value where there is one,
# after the retry of a silent database and before the plain question.
plan = body_of(POPUP, "showPlan")
retry = plan.find("dbRetryDue()")
typical = plan.find("densityTypical(s_in.material)>0")
ask = plan.find("showDensityAsk(STR_SORT_NO_DENSITY)")
assert typical >= 0, f"src/{POPUP}: showPlan() never offers a typical density"
assert "showDensityOffer()" in plan, f"src/{POPUP}: showPlan() does not call showDensityOffer()"
assert 0 <= retry < typical < ask, \
    f"src/{POPUP}: showPlan() must retry the database, then offer the typical value, then ask"

# 4. "Use": the left button takes the typical value as a typed one would be
# taken (onDensity(): range, input, plan again), ahead of opening the pad.
click = click_handler_of(POPUP, "s_job = TCP_CREATE")
take = click.find("onDensity(s_density_typical)")
pad = click.find(squeeze("showPricePadInfo(s_in.density_typed, STR_DENSITY_PAD_TITLE, STR_DENSITY_PAD_INFO, onDensity)"))
assert take >= 0, f"src/{POPUP}: the left button never takes the typical density"
assert "if(s_density_offer)" in click, f"src/{POPUP}: the left button does not look at s_density_offer"
assert click.find("s_plan.no_density") < take < pad, \
    f"src/{POPUP}: the left button must take the offered value before it opens the pad"

# 5. "Change": the price button opens our density field with the value in it,
# and only while the offer stands; otherwise the price pad as before.
price = click_handler_of(POPUP, "showPricePad(s_price, onPrice)")
change = price.find(squeeze("showPricePadInfo(s_density_typical, STR_DENSITY_PAD_TITLE, STR_DENSITY_PAD_INFO, onDensity)"))
assert change >= 0, f"src/{POPUP}: the price button never opens the density field with the typical value"
assert price.find("if(s_density_offer)") < change < price.find("showPricePad(s_price,onPrice)"), \
    f"src/{POPUP}: the price button must open the density field while the offer stands, else the price"
assert "STR_DENSITY_CHANGE" in body_of(POPUP, "refreshPrice"), \
    f"src/{POPUP}: refreshPrice() does not say 'Change' while the offer stands"

# 6. The light form of the database: a support code finds its density under
# its base material (densitySupportBase()), read through the maker's list.
product = body_of(PLANNER, "productDensity")
light = product.find("SM_FDB_LIGHT")
assert light >= 0, f"src/{PLANNER}: productDensity() skips the light form of the database"
assert product.find("densitySupportBase(in.material)") > light, \
    f"src/{PLANNER}: productDensity() does not ask for the base of a support code in the light form"
assert "spoolmanFdbEachOf(base_url,in.vendor,base," in product, \
    f"src/{PLANNER}: productDensity() does not list the maker's entries of the base material"
# The Spoolman form keeps its read limit (f04).
assert "FDB_LIMIT_ANSWER" in product, f"src/{PLANNER}: productDensity() reads without the limit"

print("typical density on the card: ok")
