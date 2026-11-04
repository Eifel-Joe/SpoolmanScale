"""Read the source tree without a compiler and check where v0.8.7-beta.3 (weighing) joins the original's o07, o13 and a11 with the fork's own parts: BamBuddy's 0 stays a tare of the spool's own (M keeps own and inherited apart), the large number on the main screen follows the same chain rule and takes the parts off (E), the weight popup closes the parts chooser too when another spool is loaded, a FilaMan spool without a tag is taken on only once it loaded, and "without type" at a create answers the first weighing only when the chain holds a tare somewhere (f06)."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer and the function finder of the convention ratchet: comments
# and string contents blanked, function bodies by brace matching.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

problems = []


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def code_of(rel):
    """One file under src/ as the ratchet reads it (comments and strings blanked), blanks gone."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return squeeze(cc.Source(path.read_text(encoding="utf-8")).code)


def raw_of(rel):
    """One file under src/ with comments taken out but strings kept, blanks gone."""
    text = (src / rel).read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return squeeze(text)


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    code = cc.Source((src / rel).read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    if len(spans) != 1:
        problems.append(f"src/{rel}: {name}() is not defined exactly once")
        return ""
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def need(cond, text):
    if not cond:
        problems.append(text)


# 1. BamBuddy's own 0 (o07 next to M). BamBuddy counts a stored 0 as a real
# empty weight in both inventories (_spoolman_helpers.py spoolman_tare(), "0 is
# a real tare"; spoolbuddy.py weighs gross - core_weight), and flags a weight
# the spool only inherits (core_weight_is_inherited). The spool level takes the
# core when it is not inherited, 0 included; the filament level always. The
# original's "core > 0" would turn a spool's own 0 into "nothing stored", and
# the first weighing would ask a refill for a type.
BB = "services/bambuddy_api.cpp"
bb = raw_of(BB)
need(squeeze('if (!inherited) dst["spool_weight"] = (float)core;') in bb,
     f"src/{BB}: mapSpool() must give the spool level the core whenever it is not inherited, 0 included")
need(squeeze('f["spool_weight"] = (float)core;') in bb and
     squeeze('if (core > 0) f["spool_weight"]') not in bb and
     squeeze('if (core > 0) dst["spool_weight"]') not in bb,
     f"src/{BB}: mapSpool() must not drop BamBuddy's 0, which BamBuddy itself counts as a real empty weight")

# 2. The large number on the main screen ("Fix 4") and the difference next to
# it: both take the tare whenever the chain named one, 0 included, and both
# work on the reading without the parts on the spool (E). With "> 0" a refill
# stored at 0 g showed the reading with its holder on it.
LOOP = "app/app_loop.cpp"
loop = code_of(LOOP)
both = squeeze("if (sm_found && sm_tare_source != TARE_NONE) { float netto = spoolExtrasBareReading() - sm_spool_weight;")
need(loop.count(both) == 2,
     f"src/{LOOP}: the large number and the difference must both read 'sm_found && sm_tare_source != TARE_NONE' "
     f"with the bare reading (found {loop.count(both)} of 2)")
need(squeeze("if (sm_spool_weight > 0) {") not in loop,
     f"src/{LOOP}: a tare of 0 g still counts as none somewhere ('sm_spool_weight > 0')")

# 3. The weight popup closes when another spool is loaded (o13), and the parts
# chooser, which only its chip opens and which counts as the popup, goes with
# it unsaved: left open it would store the parts for the new spool, and the
# tick would log on every pass.
WP = "ui/weight_popup.cpp"
tick = body_of(WP, "weightPopupTick")
need(tick and squeeze("sm_id == s_spool_id") in tick, f"src/{WP}: weightPopupTick() must compare the spool it was opened for")
need(squeeze("closeExtrasChooser();") in tick,
     f"src/{WP}: weightPopupTick() must take the parts chooser down too when another spool is loaded")
need(squeeze("closeWeightPopups();") in tick, f"src/{WP}: weightPopupTick() must close the popup")
need(loop.count(squeeze("weightPopupTick();")) == 1, f"src/{LOOP}: weightPopupTick() must run once per pass")

# 3b. The same fault one step on, found next to o13: the question "raise the
# label?" holds a reading of the spool it was asked for, and its answers, like
# the weight the popup queues, run a pass later on whatever spool is loaded
# then. They carry their spool now, the question closes when another spool is
# loaded, and the job slot drops a weight meant for another spool, as it
# already did for an empty weight.
CP = "ui/confirm_popup.cpp"
queue = body_of(CP, "confirmQueueWeight")
need(squeeze("s_job_spool_id = sm_id;") in queue, f"src/{CP}: confirmQueueWeight() must carry the spool the weight is for")
cap = body_of(CP, "showBamBuddyCapPopup")
need(squeeze("s_cap_spool_id = spool_id;") in cap and squeeze("s_job_spool_id = s_cap_spool_id;") in cap,
     f"src/{CP}: the label question must remember its spool and hand it to the job")
need(squeeze("showBamBuddyCapPopup(bb_cap_measured_g, bb_cap_label_g, bb_cap_spool_id);") in loop,
     f"src/{LOOP}: the label question must be opened for the spool the check was made for")
need(squeeze("bb_cap_spool_id = sm_id;") in code_of("services/spoolman_actions.cpp"),
     "src/services/spoolman_actions.cpp: the check above the label must note the spool")
jobs = body_of(CP, "handleConfirmPopupDeferredActions")
need(squeeze("job == CJ_WEIGHT || job == CJ_CAP_RAISE || job == CJ_CAP_KEEP") in jobs and
     squeeze("if (spool_job && sm_id != s_job_spool_id)") in jobs,
     f"src/{CP}: a parked weight must be dropped when another spool is loaded, as a parked tare is")
need(squeeze("if (s_cap_popup && sm_id != s_cap_spool_id)") in jobs,
     f"src/{CP}: the label question must close when another spool is loaded")

# 3c. Everything else the weight popup and the spool's questions do, found
# by the review: the popup's other buttons (archive, options, full spool, the
# empty weight rows, the parts chip) act on the spool loaded when they run, and
# a stale popup still takes taps in the pass before its tick; the questions
# "archive?" and "dried today?" stayed open over a swap and acted on the new
# spool; "full spool" (CJ_INITIAL), the archive (CJ_ARCHIVE) and the
# reactivation carried no spool.
stale = body_of(WP, "wpStale")
need(stale and squeeze("if (sm_id == s_spool_id) return false;") in stale and squeeze("closeWeightPopups();") in stale,
     f"src/{WP}: wpStale() must close a popup that belongs to another spool")
wp_code = code_of(WP)
need(wp_code.count(squeeze("if (wpStale()) return;")) >= 7,
     f"src/{WP}: every button of the popup must first ask wpStale() "
     f"(found {wp_code.count(squeeze('if (wpStale()) return;'))} of at least 7)")
need(body_of(WP, "wpPrimaryClicked").startswith(squeeze("if (wpStale()) return;")),
     f"src/{WP}: wpPrimaryClicked() must ask wpStale() first")
show = body_of(CP, "showConfirmPopup")
need(squeeze("s_confirm_spool_id = sm_id;") in show, f"src/{CP}: showConfirmPopup() must note the spool it asks about")
cp_code = code_of(CP)
need(squeeze("if ((act == 1 || act == 3) && sm_id != s_confirm_spool_id) return;") in cp_code,
     f"src/{CP}: 'yes' to 'archive?' or 'dried today?' must not act on another spool")
need(squeeze("s_job = CJ_ARCHIVE; s_job_spool_id = s_confirm_spool_id;") in cp_code,
     f"src/{CP}: the archive must carry the spool it was asked for")
need(squeeze("if (confirm_popup && (confirm_action == 1 || confirm_action == 3) && sm_id != s_confirm_spool_id)") in jobs,
     f"src/{CP}: 'archive?' and 'dried today?' must close when another spool is loaded")
need(squeeze("job == CJ_WEIGHT || job == CJ_CAP_RAISE || job == CJ_CAP_KEEP || job == CJ_INITIAL || job == CJ_ARCHIVE") in jobs,
     f"src/{CP}: a full spool's label and an archive must be dropped for another spool")
need(squeeze("s_job = CJ_INITIAL; s_job_value = netto_g; s_job_spool_id = sm_id;") in body_of(CP, "confirmNewSpool"),
     f"src/{CP}: confirmNewSpool() must carry the spool")
need(squeeze("if (spool_id != sm_id) {") in cap and squeeze("return;") in cap,
     f"src/{CP}: showBamBuddyCapPopup() must not open for a spool loaded since the check")
need(squeeze("reactivate_spool_id = sm_id;") in body_of(WP, "wpPrimaryClicked"),
     f"src/{WP}: the reactivation must carry its spool")
need(squeeze("if (reactivate_spool_id != sm_id) {") in loop,
     f"src/{LOOP}: a reactivation for another spool must be dropped")
need(squeeze("tareMissingTick(); weightPopupTick();") in loop, f"src/{LOOP}: weightPopupTick() must run every pass")

# 4. FilaMan, a spool picked without a tag (a11, AreYouThereAndi2): the scale
# takes it on for weighing only once it loaded. A failed request leaves the last
# spool's values in place, and weighing then wrote into that one. The status
# line says "weighed" only then too.
q = loop.find(squeeze("querySpoolmanById(id);"))
guard = loop.find(squeeze("if (!sm_found || sm_id != id)"), q)
adopt = loop.find(squeeze("aw_adopted = true;"), q)
weigh = loop.find(squeeze("copyT(buf, sizeof(buf), STR_REMOTE_LINK_WEIGH);"), q)
need(q >= 0 and 0 <= guard < adopt < weigh,
     f"src/{LOOP}: the tagless FilaMan spool must be checked as loaded (sm_found, sm_id == id) "
     f"before it is taken on and before the status says it is weighed")
# The adoption sits in the else of that check, not after a check that only logs.
between = loop[guard:adopt] if 0 <= guard < adopt else ""
need(squeeze("} else {") in between and "aw_adopted" not in between,
     f"src/{LOOP}: the tagless FilaMan spool must be taken on only in the else of the check")

# 5. f06: "without type" at a create answers the first weighing only when the
# spool, its filament or its brand has an empty weight. Both creates that show
# the list hand their answer to tareEntryCreateAnswered(), which reads the new
# spool off Spoolman behind BamBuddy and lets the rule decide.
TE = "ui/tare_entry.cpp"
answered = body_of(TE, "tareEntryCreateAnswered")
for call in ("bbSpoolmanUrl()", "spoolmanGetSpoolJson(", "spoolTare(", "spoolTypeCreateAnswered(",
             "tareEntryFirstWeighDone(spool_id);",
             "if (type_g <= 0 && spoolTypesActive() && g_scale_fitted && bbSpoolmanUrl()[0])",
             "if (code == 200) {"):
    need(squeeze(call) in answered, f"src/{TE}: tareEntryCreateAnswered() must use {call}")
need(squeeze("if (s_in.spool_type_asked) tareEntryCreateAnswered(r.spool_id, s_in.spool_type_g);")
     in code_of("ui/tag_create_popup.cpp"),
     "src/ui/tag_create_popup.cpp: the create must hand the list's answer to tareEntryCreateAnswered()")
need(squeeze("if (copy_create_asked) tareEntryCreateAnswered(new_id, (int)lroundf(copy_create_core));")
     in code_of("ui/copy_flow.cpp"),
     "src/ui/copy_flow.cpp: the copy must hand the list's answer to tareEntryCreateAnswered()")
callers = sorted(str(p.relative_to(src)).replace("\\", "/") for p in src.rglob("*.cpp")
                 if squeeze("tareEntryFirstWeighDone(") in code_of(str(p.relative_to(src))))
need(callers == [TE], f"tareEntryFirstWeighDone() is called outside {TE}: {callers}")

if problems:
    for p in problems:
        print("FAIL", p)
    raise AssertionError(f"{len(problems)} finding(s) in the weighing paths")
print("weighing paths: ok")
