"""Read the source tree without a compiler and check the Klipper printers in the AMS view, the bay picker and the header (ui/ams_view.cpp, services/ams_presence.cpp, services/ams_pick.cpp, services/ams_weights.cpp, ui/ams_detail_popup.cpp): the list of printers lives in the PSRAM (64 of them no longer fit the BSS), an error or a missing AMS leaves the printer button in place when there are several printers, the unit of a lone field is called "Aktive Spule", a Klipper number never reaches a call that is Bambuddy's, the answer of a refused assignment is told in words, and the grams, colour and material of a field are read from its spool."""
import re

import klipper_src as ks

VIEW = "ui/ams_view.cpp"
PRESENCE = "services/ams_presence.cpp"
PICK = "services/ams_pick.cpp"
WEIGHTS = "services/ams_weights.cpp"
POPUP = "ui/ams_detail_popup.cpp"

ks.assert_ci_step("test_ams_klipper.py")

# 1. The view. The list of printers is made once in the PSRAM and never freed.
view = ks.source_of(VIEW)
code = ks.collapse(view.code)
assert not re.search(r"\bAmsPrinterList\s+s_printers\b", code), f"src/{VIEW}: s_printers is still a list in the BSS"
assert re.search(r"\bAmsPrinterList\s*\*\s*s_printers\b", code), f"src/{VIEW}: s_printers is not an AmsPrinterList*"
assert "MALLOC_CAP_SPIRAM" in code, f"src/{VIEW}: the list is not asked from the PSRAM"
assert not re.search(r"\bs_printers\s*\.", code), f"src/{VIEW}: s_printers. is still used (it is a pointer: s_printers->)"
assert "s_printers->" in code, f"src/{VIEW}: s_printers-> is never used"

# The button stays where there are several printers: the status line may leave it.
assert re.search(r"\bstatic\s+void\s+statusRowPlain\s*\(\s*bool\s+keep_chip\s*\)", code), \
    f"src/{VIEW}: statusRowPlain() does not take 'bool keep_chip'"
assert re.search(r"\bstatic\s+void\s+setStatus\s*\(\s*const\s+char\s*\*\s*\w+\s*,\s*bool\s+keep_chip\s*=\s*false\s*\)", code), \
    f"src/{VIEW}: setStatus() is not (const char* text, bool keep_chip = false)"
plain = ks.body_of(VIEW, "statusRowPlain")
assert "keep_chip" in plain and plain.find("keep_chip") < plain.find("lv_obj_add_flag(s_printer_btn"), \
    f"src/{VIEW}: statusRowPlain() hides the printer button without asking keep_chip"
assert "statusRowPlain(keep_chip)" in ks.squeeze(ks.body_of(VIEW, "setStatus")), \
    f"src/{VIEW}: setStatus() does not hand keep_chip on to statusRowPlain()"

fetch = ks.body_of(VIEW, "fetchAndDraw")
assert re.search(r"s_printers->count\s*>\s*1", fetch), f"src/{VIEW}: fetchAndDraw() does not keep the button for s_printers->count > 1"
for text, name in (("STR_AMSV_ERR_NET", "no connection"), ("STR_AMSV_NO_AMS", "no AMS")):
    found = [args for _, args in ks.calls(fetch, "setStatus") if text in args[0]]
    assert found, f"src/{VIEW}: fetchAndDraw() has no setStatus() for {name}"
    for args in found:
        assert len(args) >= 2, f"src/{VIEW}: the setStatus() for {name} does not say keep_chip"
found = [args for _, args in ks.calls(fetch, "setStatusFmt") if "STR_AMSV_ERR_HTTP" in args[0]]
assert found, f"src/{VIEW}: fetchAndDraw() has no setStatusFmt() for a failed fetch"
# The failed fetch keeps the button too: either every call says so (a third
# argument), or setStatusFmt() decides it itself, on purpose: what it hands on
# to statusRowPlain() is neither false or 0 nor one of its own parameters (a
# parameter with the default false would hide the button when the call leaves
# it out).
head = re.search(r"\bstatic\s+void\s+setStatusFmt\s*\(([^)]*)\)", view.code)
assert head, f"src/{VIEW}: setStatusFmt() is not there"
own = {re.findall(r"[A-Za-z_]\w*", p.split("=")[0])[-1] for p in head.group(1).split(",") if p.strip()}
fmt = ks.calls(ks.body_of(VIEW, "setStatusFmt"), "statusRowPlain")
fmt_keeps = bool(fmt) and all(
    call[1] and call[1][0].strip() not in ("", "false", "0") and call[1][0].strip() not in own for call in fmt)
for args in found:
    assert len(args) >= 3 or fmt_keeps, \
        f"src/{VIEW}: a setStatusFmt(STR_AMSV_ERR_HTTP, ...) for a failed fetch does not keep the printer button"

# Names: a lone field is "Aktive Spule" as unit and as bay.
unit = ks.body_of(VIEW, "unitName")
assert "AMS_MODEL_KLIPPER" in unit and "STR_AMSV_UNIT_KL_ACTIVE" in unit, \
    f"src/{VIEW}: unitName() does not call a Klipper unit without a name 'Aktive Spule'"
bay = ks.body_of(VIEW, "bayName")
for token in ("klipperIsPrinterId(", "klipperFieldName(", "STR_AMSV_UNIT_KL_ACTIVE"):
    assert token in bay, f"src/{VIEW}: bayName() does not use {token}"
assert "klipperTruncated(" in code and "STR_AMSV_KL_FIRST16" in code, \
    f"src/{VIEW}: the status line does not say 'nur die ersten 16 Felder' when klipperTruncated()"

# What only Bambuddy or the AMS source can answer is not asked for a Klipper number.
for call in ("backendFindUnitSpools", "backendFindBaySpool"):
    sites = [(a, b) for _, a, b in ks.cc.find_functions(view.code) if re.search(rf"\b{call}\s*\(", view.code[a:b])]
    assert len(sites) == 1, f"src/{VIEW}: {call}() is not called in exactly one function"
    text = view.code[sites[0][0]:sites[0][1]]
    guard = text.find("klipperIsPrinterId(")
    assert 0 <= guard < re.search(rf"\b{call}\s*\(", text).start(), \
        f"src/{VIEW}: {call}() is called without klipperIsPrinterId() asked before it"

# What a field's spool gives: grams, and colour and material where the field has none.
applied = ks.body_of(VIEW, "applyWeights")
assert re.search(r"\bcolor\.valid\b", applied) and re.search(r"\bname\[0\]", applied), \
    f"src/{VIEW}: applyWeights() does not take colour and name only into the fields that have none"

# 2. The header: a printer of the Klipper kind is there without asking a network.
presence = ks.source_of(PRESENCE)
assert not re.search(r"\bAmsPrinterList\s+s_printers\b", ks.collapse(presence.code)), \
    f"src/{PRESENCE}: s_printers is still a list in the BSS"
assert "MALLOC_CAP_SPIRAM" in presence.code, f"src/{PRESENCE}: the list is not asked from the PSRAM"
tick = ks.body_of(PRESENCE, "amsPresenceTick")
has = tick.find("klipperHasPrinters(")
assert has >= 0, f"src/{PRESENCE}: amsPresenceTick() does not ask klipperHasPrinters()"
for call in ("backendListPrinters(", "backendGetAmsState("):
    at = tick.find(call)
    assert at < 0 or has < at, f"src/{PRESENCE}: amsPresenceTick() asks {call} before klipperHasPrinters()"

# 3. The bay picker: what a refused assignment says, and that a Klipper printer
# gets no location question from the old way.
rows = ks.lang_rows()
fail = ks.body_of(PICK, "assignFailText")
fail_squeezed = ks.squeeze(fail)
way = fail.find("backendMode(")
TEXTS = (("KL_E_NOT_HELD", "Drucker hat die Spule nicht übernommen"),
         ("KL_E_OTHER_SPOOLMAN", "Anderer Spoolman - Zuordnen gesperrt"))
ids = {}
for code_name, german in TEXTS:
    found = ks.lang_ids_with_german(rows, re.escape(german))
    assert len(found) == 1, f"src/lang.cpp: {len(found)} rows with the text '{german}', expected 1"
    ids[german] = found[0]
    # Any form of mapping: a case, an if, a table row. The first string id after
    # the code, before the next semicolon, is the text.
    mapped = re.search(rf"\b{code_name}\b[^;]*?\b(STR_\w+)\b", fail)
    assert mapped and mapped.group(1) == found[0], \
        f"src/{PICK}: assignFailText() does not answer {code_name} with {found[0]} ('{german}')"
    assert way < 0 or 0 <= fail.find(code_name) < way, \
        f"src/{PICK}: {code_name} is answered after the test of backendMode(): a Klipper printer in Bambuddy's mode would never get here"
for german in ("Keine Antwort - bitte am Drucker prüfen", "Zugang verweigert"):
    found = ks.lang_ids_with_german(rows, re.escape(german))
    assert len(found) == 1, f"src/lang.cpp: {len(found)} rows with the text '{german}', expected 1"
    ids[german] = found[0]
    assert found[0] in fail, f"src/{PICK}: assignFailText() never answers with {found[0]} ('{german}')"
assert re.search(r"code\s*<\s*=?\s*0|case\s+-\d+", fail), \
    f"src/{PICK}: assignFailText() does not tell a call that got no answer (a negative code) from the others"
assert re.search(r"\b401\b", fail), f"src/{PICK}: assignFailText() does not know 401"
assert re.search(r"\b403\b", fail), f"src/{PICK}: assignFailText() does not know 403"
assert "klipperIsPrinterId(" in ks.code_of(PICK), f"src/{PICK}: nothing tells a Klipper printer from the others"
for sid in ids.values():
    ks.assert_row_complete(rows, sid, "src/lang.cpp")
    assert ks.enum_has(sid), f"src/lang.h: {sid} is not in the enum"

picked = ks.squeeze(ks.body_of(PICK, "onPicked"))
at = picked.find("setAmsLocation(")
assert at >= 0, f"src/{PICK}: onPicked() does not call setAmsLocation()"
assert 0 <= picked.find("klipperIsPrinterId(") < at, \
    f"src/{PICK}: onPicked() asks klipperIsPrinterId() only after setAmsLocation() (klipperAssign() writes the location itself)"
assert "STR_AMSV_ASSIGNED" in picked and "g_loc_popup_shown_for_id=spool_id;" in picked, \
    f"src/{PICK}: a done assignment no longer says 'Zugeordnet' without the location question"

# 4. Grams, colour and material of the field's spool.
weights_h = ks.code_of("services/ams_weights.h")
item = re.search(r"struct\s+AmsWeightItem\s*\{([^{}]*)\}", weights_h)
assert item, "src/services/ams_weights.h: struct AmsWeightItem is not there"
assert re.search(r"\bSpoolColor\s+color\s*;", item.group(1)), "src/services/ams_weights.h: AmsWeightItem has no 'SpoolColor color;'"
assert re.search(r"\bchar\s+name\s*\[\s*AMS_NAME_MAX\s*\]\s*;", item.group(1)), \
    "src/services/ams_weights.h: AmsWeightItem has no 'char name[AMS_NAME_MAX];'"
start = ks.body_of(WEIGHTS, "amsWeightsStart")
bb = start.find("backendIsBamBuddy(")
assert bb >= 0, f"src/{WEIGHTS}: amsWeightsStart() no longer asks backendIsBamBuddy()"
klipper = start.find("klipperIsPrinterId(")
assert 0 <= klipper < start.find(";", bb), \
    f"src/{WEIGHTS}: amsWeightsStart() refuses a Klipper printer with the Bambuddy-only test"
# Beyond the list of Review Focus 3, from it: the weights task asks Bambuddy
# for the printer's assignments first, and a Klipper number would go there.
task = ks.body_of(WEIGHTS, "amsWeightsTask")
find = task.find("backendFindPrinterSpools(")
assert find >= 0, f"src/{WEIGHTS}: amsWeightsTask() no longer asks backendFindPrinterSpools()"
guard = task.find("klipperIsPrinterId(")
assert 0 <= guard < find, \
    f"src/{WEIGHTS}: amsWeightsTask() asks backendFindPrinterSpools() for a Klipper number (klipperIsPrinterId() must come first)"

# 5. The detail card: no "Herausnehmen" for a field of a Klipper printer.
assert "!klipperIsPrinterId(" in ks.body_of(POPUP, "canTakeOut"), \
    f"src/{POPUP}: canTakeOut() offers 'Herausnehmen' for a Klipper printer's field"

# 6. The two texts of the view, in three languages, with their widths.
budgets = (ks.root / "tools" / "ui_budgets.tsv").read_text(encoding="utf-8")
for sid, german in (("STR_AMSV_UNIT_KL_ACTIVE", "Aktive Spule"), ("STR_AMSV_KL_FIRST16", "nur die ersten 16 Felder")):
    assert sid in rows, f"src/lang.cpp: {sid} has no row"
    assert rows[sid][0] == german, f"src/lang.cpp: {sid} reads '{rows[sid][0]}', expected '{german}'"
    ks.assert_row_complete(rows, sid, "src/lang.cpp")
    assert ks.enum_has(sid), f"src/lang.h: {sid} is not in the enum"
    assert re.search(rf"^{sid}\t\d+\t\d+\t(?:NEVER|DOT|WRAP:\d+)\t", budgets, re.M), \
        f"tools/ui_budgets.tsv: no width budget for {sid}"

print("test_ams_klipper: ok")
