"""Read the source tree without a compiler and check that the card for a new filament asks for a density it cannot find (V34): when neither the filament database nor Spoolman's materials know one, the left button reads "Density" and opens the price pad under its own title, a plausible answer is kept in the input and the plan is asked again, an implausible one says the range and changes nothing, and the plan takes the typed density last, in both of its branches (a filament from a tag, and a picked database entry without a density)."""
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

POPUP = "ui/tag_create_popup.cpp"
PLANNER = "services/spoolman_filament.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def source_of(rel):
    """One file under src/ as the ratchet reads it."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def body_of(rel, name):
    """The code between the braces of the one function called name."""
    code = source_of(rel).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def click_handler_of(rel, marker):
    """The body of the one event handler (lambda or named) that contains marker."""
    source = source_of(rel)
    found = [squeeze(source.code[b0 + 1:b1]) for _, _, b0, b1 in cc.handlers(source)
             if squeeze(marker) in squeeze(source.code[b0 + 1:b1])]
    assert len(found) == 1, f"src/{rel}: {len(found)} event handlers contain {marker}, expected one"
    return found[0]


# 1. The left button of the card: with a density missing it opens the price
# pad under the density title, with the answer going to onDensity(); only
# after that does the click go on to the create. The pad is opened from the
# click (the price button does the same) and no work runs in the handler: the
# create is parked in s_job as before.
click = click_handler_of(POPUP, "s_job = TCP_CREATE")
ask = squeeze("showPricePadTitled(s_in.density_typed, STR_DENSITY_PAD_TITLE, onDensity)")
assert "s_plan.no_density" in click, \
    f"src/{POPUP}: the left button does not look at s_plan.no_density"
assert ask in click, \
    f"src/{POPUP}: the left button does not open the price pad as {ask}"
assert click.find("s_plan.no_density") < click.find(ask) < click.find("s_job=TCP_CREATE"), \
    f"src/{POPUP}: the left button must ask for the density before it creates"
assert "if(s_job!=TCP_IDLE)return;" in click and click.find("if(s_job!=TCP_IDLE)return;") < click.find("s_plan.no_density"), \
    f"src/{POPUP}: the left button must ignore a click while the loop is busy, before it asks for anything"

# 2. The answer of the pad: an emptied field changes nothing, an implausible
# value says the range, a plausible one is kept in the input (so a plan asked
# again before the create still has it) and the plan is asked again.
body = body_of(POPUP, "onDensity")
for needle in ("densityPlausible(", "s_in.density_typed=", "s_plan_stale=true", "s_job=TCP_PLAN", "STR_DENSITY_RANGE"):
    assert squeeze(needle) in body, f"src/{POPUP}: onDensity() does not use {needle}"
assert body.find("densityPlausible(") < body.find("s_in.density_typed="), \
    f"src/{POPUP}: onDensity() sets s_in.density_typed before it asks densityPlausible()"
assert body.find("s_in.density_typed=") < body.find("s_job=TCP_PLAN"), \
    f"src/{POPUP}: onDensity() asks for a plan before the density is kept in the input"
assert body.find("STR_DENSITY_RANGE") < body.find("s_in.density_typed="), \
    f"src/{POPUP}: onDensity() must say the range on the way out, before the density is kept"
assert re.search(r"\w+<=0(?:\.0f)?\)return;", body), \
    f"src/{POPUP}: onDensity() does not leave an emptied field (0) alone"
# The card is the only caller of the pad for a density, and it must not be
# able to reset the typed value on the way: the input is set once, by the pad.
assert len(re.findall(r"s_in\.density_typed=", squeeze(source_of(POPUP).code))) == 1, \
    f"src/{POPUP}: s_in.density_typed is set in more than one place"

# 3. The left button reads "Density" only while the card asks, and "Create"
# in every other state: showPlan() names the density for the one state that
# needs it, and showStatus(), which every other state goes through, puts
# the create text back.
plan = body_of(POPUP, "showPlan")
assert "s_plan.no_density" in plan and "STR_SORT_NO_DENSITY" in plan, \
    f"src/{POPUP}: showPlan() lost the no-density branch"
assert "STR_DENSITY_BTN" in squeeze(source_of(POPUP).code), \
    f"src/{POPUP}: the left button is never named STR_DENSITY_BTN"
status = body_of(POPUP, "showStatus")
assert "STR_COPY_CARD_CREATE" in status, \
    f"src/{POPUP}: showStatus() does not put the create text back on the left button"

# 4. The plan takes the typed density last, in both branches: a filament from a
# tag asks the product name before it, and a picked database entry takes the
# typed density when neither the database nor the materials have one.
body = body_of(PLANNER, "planFromTag")
assert "productDensity(" in body and "takeTypedDensity(" in body, \
    f"src/{PLANNER}: planFromTag() does not ask productDensity() and then takeTypedDensity()"
assert body.find("productDensity(") < body.find("takeTypedDensity("), \
    f"src/{PLANNER}: planFromTag() takes the typed density before the product name was asked"
body = body_of(PLANNER, "takeTypedDensity")
assert "density_typed" in body and "densityPlausible(" in body, \
    f"src/{PLANNER}: takeTypedDensity() does not check a typed density with densityPlausible()"
body = body_of(PLANNER, "spoolmanPlanTagFilament")
assert squeeze("if (picked) takeTypedDensity(") in body, \
    f"src/{PLANNER}: the picked branch of spoolmanPlanTagFilament() does not take a typed density"
assert body.find("materialDensity(") < body.find("takeTypedDensity("), \
    f"src/{PLANNER}: the picked branch takes the typed density before the materials were asked"

print("density card: ok")
