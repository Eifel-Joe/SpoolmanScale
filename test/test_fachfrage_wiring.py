"""Read the source tree without a compiler and check how v0.8.7-beta.4 (Spec 5.3 AC, V71: j07, j09, j11 by Jensilein, a03, a06, a16 by AreYouThereAndi2) is wired in.

- j07: an assignment through the bay question is watched only for a BamBuddy printer, never for a Klipper printer or an AMS source; the recheck runs from the loop under the conditions the questions use, reads the AMS into PSRAM, and ends for a spool that is taken out or "not into the AMS".
- j09: "Loading AMS bays" from the lift until the bays are drawn, only for an armed bay question, gone when the spool comes back, at most 6 s.
- j11: "Not into the AMS" only in the question after a lift; opened by "Into AMS" it stays Cancel. Both keep the leave style (V43).
- a03: heartbeat and weight from a task whose stack lies in PSRAM, commands handled in the loop, without the task the same short timeouts inline.
- a06: the busy ring only for the loop's own waits, the outermost bracket.
- a16: "Take out" in the card's header with BamBuddy and an AMS source, the original's question behind it, not for a Klipper field, not with FilaMan; German "Austragen".
"""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

PICK = "services/ams_pick.cpp"
LOOP = "app/app_loop.cpp"
VIEW = "ui/ams_view.cpp"
CARD = "ui/ams_detail_popup.cpp"
DEVICE = "services/bambuddy_device.cpp"
PROGRESS = "services/http_progress.cpp"
BADGE = "ui/busy_badge.cpp"
BACKEND = "services/backend_api.cpp"
DOC = "web/web_doc.cpp"


def squeeze(text):
    return re.sub(r"\s+", "", text)


def text_of(rel):
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return path.read_text(encoding="utf-8")


def code_of(rel):
    return cc.Source(text_of(rel)).code


def body_of(rel, name):
    code = code_of(rel)
    spans = [(s, e) for found, s, e in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def has(body, piece, where):
    assert squeeze(piece) in body, f"{where}: '{piece}' is not there"


def hasnt(body, piece, where):
    assert squeeze(piece) not in body, f"{where}: '{piece}' should not be there"


def before(body, first, second, where):
    a, b = body.find(squeeze(first)), body.find(squeeze(second))
    assert a >= 0, f"{where}: '{first}' is not there"
    assert b >= 0, f"{where}: '{second}' is not there"
    assert a < b, f"{where}: '{first}' stands after '{second}'"


failures = []


def check(fn):
    try:
        fn()
    except AssertionError as e:
        failures.append(f"{fn.__name__}: {e}")


# ---- j07 ---------------------------------------------------------------------
def j07_started_for_bambuddy_only():
    b = body_of(PICK, "onPicked")
    has(b, "if (!klipper && backendIsBamBuddy())", f"src/{PICK}: onPicked()")
    has(b, "amsRecheckStart(", f"src/{PICK}: onPicked()")


def j07_tick():
    b = body_of(PICK, "amsPickRecheckTick")
    w = f"src/{PICK}: amsPickRecheckTick()"
    before(b, "if (!backendIsBamBuddy() || !backendCanAssignAmsSlot())", "amsRecheckDue(", w)
    has(b, "amsRecheckAfterFind(", w)
    has(b, "amsRecheckAfterBay(", w)
    has(b, "amsRecheckAfterPresence(", w)
    has(b, "backendAssignAmsSlot(", w)
    has(b, "amsRecheckLooked(", w)
    # The AMS of the printer, about 1.8 kB, comes from PSRAM for the look and
    # goes again: no third fixed copy in the internal RAM of the 7B.
    code = squeeze(code_of(PICK))
    assert "MALLOC_CAP_SPIRAM" in code and "heap_caps_free(" in code, \
        f"src/{PICK}: the AMS state for the recheck does not come from PSRAM"
    hasnt(code, "AmsSlotStates_recheck_state;", f"src/{PICK}")
    # The acceptance switch for an occupied bay (Spec 5.3 AA, V71).
    has(body_of(PICK, "bayHoldsSpool"), "docBayPresence(", f"src/{PICK}: bayHoldsSpool()")


def j07_loop():
    b = body_of(LOOP, "appLoop")
    has(b, "if (wifi_ok && !uiModalWaiting() && !isAmsViewOpen() && !driedBatchBusy()) amsPickRecheckTick();",
        f"src/{LOOP}: appLoop()")


def j07_stops():
    has(body_of(PICK, "amsPickRecheckStop"), "amsRecheckStop(", f"src/{PICK}: amsPickRecheckStop()")
    # "Not into the AMS" ends the watch of the spool asked about.
    b = body_of(PICK, "onPicked")
    has(b, "amsViewTakeNotAms()", f"src/{PICK}: onPicked()")
    has(b, "amsPickRecheckStop(", f"src/{PICK}: onPicked()")
    # Taken out of its bay: the watch must not send it back.
    b = body_of(CARD, "handleAmsDetailDeferredActions")
    before(b, "if (code == 200)", "amsPickRecheckStop(spool_id);", f"src/{CARD}: handleAmsDetailDeferredActions()")


# ---- j09 ---------------------------------------------------------------------
def j09_hint():
    b = body_of(PICK, "amsPickHintShow")
    has(b, "loadingOverlayShow(T(STR_AMSPICK_LOADING))", f"src/{PICK}: amsPickHintShow()")
    t = body_of(PICK, "amsPickTick")
    has(t, "if (s_hint_up && (millis() - s_hint_ms > kHintMaxMs || (isAmsViewOpen() && !amsViewLoading()))) amsPickHintHide();",
        f"src/{PICK}: amsPickTick()")
    assert "kHintMaxMs=6000" in squeeze(code_of(PICK)), f"src/{PICK}: the hint is not limited to 6 s"
    s = body_of(PICK, "amsPickShow")
    assert s.count("amsPickHintHide();") >= 2, f"src/{PICK}: amsPickShow() does not take the hint down on every way out"
    loop = body_of(LOOP, "appLoop")
    # Shown only when the bay question is armed for the spool that left the pad.
    has(loop, "pick_popup_pending_id == sm_id", f"src/{LOOP}: appLoop()")
    has(loop, "amsPickHintShow();", f"src/{LOOP}: appLoop()")
    has(loop, "if (amsPickHintUp() && !weightSaysSpoolGone()) amsPickHintHide();", f"src/{LOOP}: appLoop()")
    # Kept in front of the page until its bays are drawn (2d9f7422).
    v = body_of(VIEW, "handleAmsViewDeferredActions")
    before(v, "hideAllOverlays();", "loadingOverlayRaise();", f"src/{VIEW}: handleAmsViewDeferredActions()")
    has(body_of(VIEW, "amsViewLoading"), "s_build_pending || s_fetch_pending", f"src/{VIEW}: amsViewLoading()")


# ---- j11 ---------------------------------------------------------------------
def j11_button():
    b = body_of(VIEW, "buildScreen")
    has(b, "s_pick_by_tap ? STR_CANCEL : STR_AMSV_NOT_AMS", f"src/{VIEW}: buildScreen()")
    has(b, "s_pick_by_tap ? footCancelCb : footNotAmsCb", f"src/{VIEW}: buildScreen()")
    has(b, "addCancelFill(leaveCancel(footButton(", f"src/{VIEW}: buildScreen()")
    n = body_of(VIEW, "footNotAmsCb")
    has(n, "s_not_ams = true;", f"src/{VIEW}: footNotAmsCb()")
    has(n, "s_close_pending = true;", f"src/{VIEW}: footNotAmsCb()")
    before(body_of(PICK, "openPicker"), "amsViewSetPickByTap(s_pending.by_tap);", "requestAmsView(",
           f"src/{PICK}: openPicker()")


# ---- a03 ---------------------------------------------------------------------
def a03_task():
    code = squeeze(code_of(DEVICE))
    w = f"src/{DEVICE}"
    assert "xTaskCreatePinnedToCoreWithCaps(" in code and "MALLOC_CAP_SPIRAM" in code, \
        f"{w}: the report task does not take its stack from PSRAM"
    assert "xTaskCreatePinnedToCore(" not in code, f"{w}: a task with an internal stack"
    assert "WORKER_STACK_OUTSIDE" in code, f"{w}: boards without PSRAM are not kept apart"
    run = body_of(DEVICE, "runJob")
    has(run, "BB_HEARTBEAT_TIMEOUT_MS", f"{w}: runJob()")
    has(run, "BB_WEIGHT_TIMEOUT_MS", f"{w}: runJob()")
    for name in ("runJob", "reportTask"):
        b = body_of(DEVICE, name)
        for loop_only in ("handleCommand(", "bbRegisterDevice(", "bbTagRemoved(", "logSD"):
            hasnt(b, loop_only, f"{w}: {name}()")
    tick = body_of(DEVICE, "bambuddyDeviceTick")
    has(tick, "handleCommand(", f"{w}: bambuddyDeviceTick()")
    has(tick, "bbHeartbeatGapMs(", f"{w}: bambuddyDeviceTick()")
    has(tick, "bbWeightGapMs(", f"{w}: bambuddyDeviceTick()")
    # Without a task the request runs here, as it always did.
    has(body_of(DEVICE, "submitJob"), "runJob(s_job);", f"{w}: submitJob()")


# ---- a06 ---------------------------------------------------------------------
def a06_badge():
    b = body_of(PROGRESS, "httpStallBegin")
    before(b, "if (!onLoopTask()) return;", "busyBadgeShow();", f"src/{PROGRESS}: httpStallBegin()")
    before(b, "if (s_stall_depth == 0) {", "busyBadgeShow();", f"src/{PROGRESS}: httpStallBegin()")
    e = body_of(PROGRESS, "httpStallEnd")
    before(e, "if (s_stall_depth != 0) return;", "busyBadgeHide();", f"src/{PROGRESS}: httpStallEnd()")
    s = body_of(BADGE, "busyBadgeShow")
    has(s, "lv_layer_top()", f"src/{BADGE}: busyBadgeShow()")
    has(s, "lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_CLICKABLE);", f"src/{BADGE}: busyBadgeShow()")


# ---- a16 ---------------------------------------------------------------------
def a16_takeout():
    b = body_of(BACKEND, "backendCanTakeOutOfAms")
    has(b, "backendMode() == BACKEND_BAMBUDDY", f"src/{BACKEND}: backendCanTakeOutOfAms()")
    has(b, "spoolmanHasAmsSource() && amsSourceCanUnassign()", f"src/{BACKEND}: backendCanTakeOutOfAms()")
    has(body_of(CARD, "canTakeOut"), "!klipperIsPrinterId(amsViewPrinterId())", f"src/{CARD}: canTakeOut()")
    h = body_of(CARD, "buildHeader")
    has(h, "if (d.status_id == 0 && canTakeOut(d))", f"src/{CARD}: buildHeader()")
    has(h, "takeoutBtnCb", f"src/{CARD}: buildHeader()")
    has(h, "STR_AMSD_TAKEOUT_BTN", f"src/{CARD}: buildHeader()")
    t = body_of(CARD, "takeoutBtnCb")
    before(t, "if (!canTakeOut(s_det)) return;", "showSpoolAsk(AMSD_ASK_TAKEOUT);", f"src/{CARD}: takeoutBtnCb()")


# ---- texts and the acceptance switch -------------------------------------------
def texts():
    lang = text_of("lang.cpp")
    for row, de in (("STR_AMSV_NOT_AMS", "Nicht ins AMS"), ("STR_AMSPICK_LOADING", "AMS-Fächer werden geladen"),
                    ("STR_AMSD_TAKEOUT_BTN", "Austragen")):
        m = re.search(r"\{\s*\"([^\"]*)\"[^}]*\}\s*,\s*//\s*" + row + r"\b", lang)
        assert m, f"src/lang.cpp: no row {row}"
        assert m.group(1).startswith(de), f"src/lang.cpp: {row} reads '{m.group(1)}', not '{de}'"
        assert re.search(r"\b" + row + r"\b", text_of("lang.h")), f"src/lang.h: {row} missing"


def sim_bay():
    doc = text_of(DOC)
    assert '"/api/sim/bay"' in doc, f"src/{DOC}: no route /api/sim/bay"
    m = re.search(r'srv\.on\("/api/sim/bay".*?\n  \}\);', doc, re.S)
    assert m and "GATE_MAINT" in m.group(0), f"src/{DOC}: /api/sim/bay is not behind the maintenance gate"
    assert "logSD" in m.group(0), f"src/{DOC}: /api/sim/bay does not log"


for fn in (j07_started_for_bambuddy_only, j07_tick, j07_loop, j07_stops, j09_hint, j11_button,
           a03_task, a06_badge, a16_takeout, texts, sim_bay):
    check(fn)
for f in failures:
    print("FAIL", f)
if failures:
    sys.exit(1)
print("test_fachfrage_wiring: ok")
