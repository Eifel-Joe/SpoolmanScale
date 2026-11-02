"""Read the source tree without a compiler and check three rests of the density at the scale (Punkteliste n05, STATUS 57):
(a) when the filament database did not answer the search by material (a time-out, no connection), the same database is not searched again by the product name, and the card does not ask for a density: it says the plan failed, and the next try asks the database again;
(b) a sort typed in whose colour the inventory has under another name asks about that twin as soon as the plan knows it, also when the plan stopped only for a missing density: taking the twin needs no density at all;
(c) a density from the database, from a picked entry or from Spoolman's material list counts only when it is plausible (density_rules.h), and the one FilaMan is sent as well; a found entry without a plausible one borrows it the way a picked entry does."""
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

PLANNER = "services/spoolman_filament.cpp"
POPUP = "ui/tag_create_popup.cpp"
FILAMAN = "services/filaman_filament.cpp"


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


def before(body, first, second, where):
    """first is in body and stands before the first appearance of second."""
    a, b = body.find(squeeze(first)), body.find(squeeze(second))
    assert a >= 0, f"{where}: '{first}' is not there"
    assert b >= 0, f"{where}: '{second}' is not there"
    assert a < b, f"{where}: '{first}' stands after '{second}'"


failures = []


def check(part, fn):
    try:
        fn()
    except AssertionError as e:
        failures.append(f"({part}) {e}")


# (a) The database's answer is kept, the product search waits for a good one,
# and a plan without a density after a failed search is a failure, not a
# question.
def a_failed_database():
    db = body_of(PLANNER, "databaseDensity")
    assert squeeze("if (code) *code = got;") in db, \
        f"src/{PLANNER}: databaseDensity() does not hand back the code of its search"
    tag = body_of(PLANNER, "planFromTag")
    before(tag, "plan->density = databaseDensity(base_url, in.vendor, in.material, &db_code);",
           "productDensity(", f"src/{PLANNER}: planFromTag()")
    assert squeeze("if (plan->density <= 0.0f && !serverReachIsNetworkFailure(db_code)) "
                   "plan->density = productDensity(base_url, in);") in tag, \
        f"src/{PLANNER}: planFromTag() searches by the product name after the database failed"
    assert squeeze("noDensity(in, db_code, plan);") in tag, \
        f"src/{PLANNER}: planFromTag() does not end through noDensity()"
    assert "no_density" not in tag, f"src/{PLANNER}: planFromTag() sets no_density itself"
    plan = body_of(PLANNER, "spoolmanPlanTagFilament")
    assert squeeze("plan->density = databaseDensity(base_url, in.vendor, in.material, &db_code);") in plan, \
        f"src/{PLANNER}: spoolmanPlanTagFilament() does not keep the database's code"
    assert squeeze("noDensity(in, db_code, plan);") in plan, \
        f"src/{PLANNER}: spoolmanPlanTagFilament() does not end through noDensity()"
    assert "no_density" not in plan, f"src/{PLANNER}: spoolmanPlanTagFilament() sets no_density itself"
    end = body_of(PLANNER, "noDensity")
    before(end, "if (serverReachIsNetworkFailure(db_code)) {", "plan->no_density = true;",
           f"src/{PLANNER}: noDensity()")
    failed, _ = end.split(squeeze("plan->no_density = true;"), 1)
    assert squeeze("plan->http_code = db_code;") in failed and squeeze("return;") in failed, \
        f"src/{PLANNER}: noDensity() does not end a failed search with its code, before the density ask"


# (b) The twin is settled for a plan that only lacks the density, and taking
# it leaves no density ask behind.
def b_twin_first():
    settle = body_of(POPUP, "settleTwin")
    assert squeeze("if (s_plan.state != TFS_CREATE_TAG && s_plan.state != TFS_CREATE_DB && "
                   "!s_plan.no_density) return;") in settle, \
        f"src/{POPUP}: settleTwin() waits for the density before it asks about the twin"
    take = body_of(POPUP, "takeTwin")
    before(take, "s_plan.no_density = false;", "showPlan();", f"src/{POPUP}: takeTwin()")


# (c) Every density the plan takes from a source passes the plausibility rule.
def c_plausible():
    count = body_of(PLANNER, "countDensity")
    assert squeeze('if (!densityPlausible(e["density"] | 0.0f)) return;') in count, \
        f"src/{PLANNER}: countDensity() counts a density outside the plausible range"
    mat = body_of(PLANNER, "materialDensity")
    assert squeeze('return plausibleOrNone(m["density"] | 0.0f);') in mat, \
        f"src/{PLANNER}: materialDensity() hands on a density outside the plausible range"
    db = body_of(PLANNER, "takeDbEntry")
    assert squeeze('plan->density = plausibleOrNone(e["density"] | 0.0f);') in db, \
        f"src/{PLANNER}: takeDbEntry() takes a database density outside the plausible range"
    picked = body_of(PLANNER, "takePickedEntry")
    assert squeeze("plan->density = plausibleOrNone(in.db_density);") in picked, \
        f"src/{PLANNER}: takePickedEntry() takes a picked density outside the plausible range"
    rule = body_of(PLANNER, "plausibleOrNone")
    assert squeeze("return densityPlausible(d) ? d : 0.0f;") in rule, \
        f"src/{PLANNER}: plausibleOrNone() does not ask densityPlausible()"
    plan = body_of(PLANNER, "spoolmanPlanTagFilament")
    assert squeeze("if (in_db && plan->density <= 0.0f) plan->density = databaseDensity(") in plan, \
        f"src/{PLANNER}: spoolmanPlanTagFilament() does not let a found entry borrow a density"
    fm = body_of(FILAMAN, "dbFilamentBody")
    assert squeeze('if (densityPlausible(in.db_density)) body["density_g_cm3"] = in.db_density;') in fm, \
        f"src/{FILAMAN}: dbFilamentBody() sends FilaMan a density outside the plausible range"


check("a", a_failed_database)
check("b", b_twin_first)
check("c", c_plausible)
for f in failures:
    print("FAIL", f)
if failures:
    sys.exit(1)
print("density rests: ok")
