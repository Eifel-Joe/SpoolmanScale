"""Read the source tree without a compiler and check how the Klipper printers are wired into the AMS backend (services/moonraker_api.cpp, services/klipper_printers.cpp, services/backend_api.cpp): Moonraker is reached with plain http and the key in the header, with only the calls the design names and a POST of the active spool only with a spool; the commands for a printer are worded in one place; the printer list carries the Klipper printers even when Bambuddy does not answer; a Klipper number never reaches a call that is Bambuddy's or the AMS source's; and an assignment is refused for another Spoolman before any command, is read back, and is read back once more; after it (V50, services/klipper_location.cpp) the spool's location becomes the printer's name directly in the scale's Spoolman, with the old location remembered in the extra field scale_prev_location, and the spool that stood in the field before gets its remembered location back."""
import re

import klipper_src as ks

API = "services/moonraker_api.cpp"
API_H = "services/moonraker_api.h"
PRINTERS = "services/klipper_printers.cpp"
PRINTERS_H = "services/klipper_printers.h"
BACKEND = "services/backend_api.cpp"
LOCATION = "services/klipper_location.cpp"
LOCATION_H = "services/klipper_location.h"
LOC_RULES_H = "services/klipper_location_rules.h"

ks.assert_ci_step("test_klipper_wiring.py")
ks.assert_ci_step("test_klipper_location_rules.py")


def first(body, pattern):
    """Offset of the first match of the regular expression in body, or -1."""
    m = re.search(pattern, body)
    return m.start() if m else -1


def need(body, pattern, where, what):
    at = first(body, pattern)
    assert at >= 0, f"{where}: {what}"
    return at


# 1. The shape of the printer list and of the model.
slots = ks.code_of("services/ams_slots.h")
assert re.search(r"#define\s+AMS_MAX_PRINTERS\s+64\b", slots), \
    "src/services/ams_slots.h: AMS_MAX_PRINTERS is not 64"
assert re.search(r"enum\s+AmsPrinterKind\s*:\s*uint8_t\s*\{[^}]*AMS_PRINTER_BAMBU\s*=\s*0[^}]*AMS_PRINTER_KLIPPER\s*=\s*1", slots), \
    "src/services/ams_slots.h: enum AmsPrinterKind : uint8_t { AMS_PRINTER_BAMBU = 0, AMS_PRINTER_KLIPPER = 1 } is not there"
printer = re.search(r"struct\s+AmsPrinter\s*\{([^{}]*)\}", slots)
assert printer, "src/services/ams_slots.h: struct AmsPrinter is not there"
for field in ("kind", "layout", "bays"):
    assert re.search(rf"\buint8_t\s+{field}\s*;", printer.group(1)), f"src/services/ams_slots.h: AmsPrinter has no 'uint8_t {field};'"
assert "AMS_MODEL_KLIPPER" in slots, "src/services/ams_slots.h: AMS_MODEL_KLIPPER is not there"

# 2. Moonraker: the numbers, the header, plain http, only the calls the design names.
api_h = ks.code_of(API_H)
assert re.search(r"#define\s+KL_CONNECT_MS\s+3000\b", api_h), f"src/{API_H}: KL_CONNECT_MS is not 3000"
assert re.search(r"#define\s+KL_ANSWER_MS\s+8000\b", api_h), f"src/{API_H}: KL_ANSWER_MS is not 8000"
for name in ("moonrakerFetchFacts", "moonrakerRunScript", "moonrakerSetActiveSpool", "moonrakerProbe"):
    assert re.search(rf"\b{name}\s*\(", api_h), f"src/{API_H}: {name}() is not declared"

api_code = ks.source_of(API).code
api_lits = ks.literals_of(API)
for token in ("KL_CONNECT_MS", "KL_ANSWER_MS"):
    assert re.search(rf"\b{token}\b", api_code), f"src/{API}: {token} is not used"
assert "X-Api-Key" in api_lits, f"src/{API}: the key does not go in the header X-Api-Key"
assert not any(re.search(r"api_?key\s*=", lit, re.I) for lit in api_lits), f"src/{API}: the key is put into an address"
assert not any("https" in lit.lower() for lit in api_lits), f"src/{API}: https is written (Moonraker is reached with http)"
assert not re.search(r"\b(?:WiFiClientSecure|setInsecure|setCACert)\b", api_code), f"src/{API}: a TLS client is used"
assert any(lit.startswith("http://") for lit in api_lits), f"src/{API}: no http:// address is built"
assert "MALLOC_CAP_SPIRAM" in api_code, f"src/{API}: the buffer for the answers is not asked from the PSRAM"
assert any(lit.startswith("KLIPPER: ") and "->" in lit for lit in api_lits), \
    f"src/{API}: no log line 'KLIPPER: GET ... -> <code>' for the answers"

# Nothing is sent that is not on this list, and nothing that changes anything
# but the active spool and the commands of moonrakerRunScript. The paths:
PATHS = ("/access/info", "/server/info", "/server/config", "/server/spoolman/status",
         "/server/spoolman/spool_id", "/server/spoolman/proxy", "/printer/objects/list",
         "/printer/objects/query", "/printer/gcode/script")
EXTRA = ("/v1/spool", "/api/v1/spool")      # the proxy's path and the scale's own Spoolman
for lit in api_lits:
    if lit.startswith("/"):
        assert any(lit == p or lit.startswith(p + "?") for p in PATHS + EXTRA), \
            f"src/{API}: the path '{lit}' is not one the design names (a call to the printer that nobody approved)"
for p in PATHS:
    assert p in api_lits, f"src/{API}: the call {p} is not made"
assert not re.search(r"\.\s*(?:PUT|PATCH|DELETE)\s*\(", api_code), f"src/{API}: a PUT, PATCH or DELETE is sent"
assert not any(lit in ("PUT", "PATCH", "DELETE") for lit in api_lits), f"src/{API}: a PUT, PATCH or DELETE is sent"
for lit in ("request_method", "use_v2_response", "limit=1&sort=id:desc"):
    assert any(lit in l for l in api_lits), f"src/{API}: the Spoolman proxy request lacks '{lit}'"

# The active spool is posted only by moonrakerSetActiveSpool, and only with a spool.
source = ks.source_of(API)
funcs = ks.cc.find_functions(source.code)
sites = ks.calls_with_literal(source.code_str, '"/server/spoolman/spool_id"')
assert len(sites) >= 2, f"src/{API}: /server/spoolman/spool_id is not both read and set by a call that names it"
for off, name, args in sites:
    if re.search(r"POST|[Pp]ost", name + " " + args):
        owner = [n for n, a, b in funcs if a <= off < b]
        assert owner == ["moonrakerSetActiveSpool"], \
            f"src/{API}: {owner} posts to /server/spoolman/spool_id (only moonrakerSetActiveSpool may)"
body = ks.body_of(API, "moonrakerSetActiveSpool", strings=True)
where = f"src/{API}: moonrakerSetActiveSpool()"
assert re.search(r"POST|[Pp]ost", body), f"{where}: does not post"
# Any guard counts that keeps a number of 0 or less from the request: an early
# return on `spool_id <= 0` as well as a request inside `if (spool_id > 0)`.
guard = need(body, r"spool_id\s*(?:<=\s*0|<\s*1|>\s*0|>=\s*1)", where, "does not turn a spool number of 0 or less away")
path_at = need(body, r'"/server/spoolman/spool_id', where, "does not name /server/spoolman/spool_id")
assert guard < path_at, f"{where}: the spool number is checked after the address is named"
others = [l for l in re.findall(r'"((?:[^"\\]|\\.)*)"', body) if "spool_id" in l and not l.startswith("/")]
assert others, f"{where}: the body of the request does not carry spool_id (without it Moonraker clears the active spool)"
body = ks.body_of(API, "moonrakerRunScript", strings=True)
assert any("script" in l and not l.startswith("/") for l in re.findall(r'"((?:[^"\\]|\\.)*)"', body)), \
    f"src/{API}: moonrakerRunScript() does not send {{\"script\": ...}}"

# M1: where the rules refuse Moonraker's Spoolman address, the text goes to the
# page and the SD log as Moonraker has it, and that may be "user:pass@host".
# The copy into the probe goes through a function that handles the '@' (cuts
# the login off), not through a plain copy of the raw text.
same = ks.body_of(API, "probeSameSpoolman", strings=True)
fallback = [name for name in re.findall(r"\b(\w+)\s*\(\s*probe->spoolman\b", same) if name != "sizeof"]
assert fallback, f"src/{API}: probeSameSpoolman() no longer keeps the printer's Spoolman address in the probe"
for name in fallback:
    assert name not in ("copyText", "strcpy", "strncpy", "snprintf", "memcpy", "strlcpy"), \
        f"src/{API}: probeSameSpoolman() copies Moonraker's raw Spoolman address with {name}() (a login 'user:pass@' would reach the page and the log)"
    assert "'@'" in ks.body_of(API, name, strings=True), \
        f"src/{API}: {name}() does not handle the '@' of an address with a login"

# The key is never written to the log, in any of the files that handle it.
for rel in (API, PRINTERS, LOCATION):
    found, leaks = ks.log_leaks(rel)
    assert found >= 1, f"src/{rel}: no log call found, this check would pass on nothing"
    assert not leaks, f"src/{rel}: a log line carries a key: {leaks}"

# 3. The commands for a printer are worded in one place.
SENT = ("MMU_", "SET_SPOOL_ID", "SET_GCODE_VARIABLE", "SAVE_VARIABLE")
for path in sorted(ks.src.rglob("*")):
    if path.suffix not in (".cpp", ".h") or "fonts" in path.relative_to(ks.src).parts:
        continue
    rel = path.relative_to(ks.src).as_posix()
    if rel == "services/klipper_rules.cpp":
        continue
    for lit in ks.literals_of(rel):
        for word in SENT:
            assert word not in lit, f"src/{rel}: the text '{word}' is written outside services/klipper_rules.cpp ('{lit[:60]}')"
rules_lits = " ".join(ks.literals_of("services/klipper_rules.cpp"))
for word in ("MMU_GATE_MAP", "MMU_SPOOLMAN", "SET_SPOOL_ID", "SET_GCODE_VARIABLE", "SAVE_VARIABLE"):
    assert word in rules_lits, f"src/services/klipper_rules.cpp: '{word}' is not there"

# 4. The glue.
printers_h = ks.code_of(PRINTERS_H)
for name in ("klipperAvailable", "klipperScaleSpoolman", "klipperIsPrinterId", "klipperHasPrinters",
             "klipperAppendPrinters", "klipperGetAmsState", "klipperAssign", "klipperFieldName",
             "klipperPrinterName", "klipperTruncated"):
    assert re.search(rf"\b{name}\s*\(", printers_h), f"src/{PRINTERS_H}: {name}() is not declared"

printers_code = ks.source_of(PRINTERS).code
for call in re.findall(r"\bbb[A-Z]\w*(?=\s*\()", printers_code) + re.findall(r"\bbb[A-Z]\w*(?=\s*\()", api_code):
    assert call in ("bbInventoryMode", "bbSpoolmanUrl"), \
        f"the Klipper files call {call}(): Bambuddy gets no call, the location goes directly to the scale's Spoolman (V50)"

b = ks.body_of(PRINTERS, "klipperAvailable")
for token in ("bbInventoryMode", "BB_INV_SPOOLMAN", "bbSpoolmanUrl"):
    assert token in b, f"src/{PRINTERS}: klipperAvailable() does not look at {token}"
assert "backendFdbBaseUrl" in ks.body_of(PRINTERS, "klipperScaleSpoolman"), \
    f"src/{PRINTERS}: klipperScaleSpoolman() is not backendFdbBaseUrl()"
assert "KL_ID_BASE" in ks.body_of(PRINTERS, "klipperIsPrinterId"), \
    f"src/{PRINTERS}: klipperIsPrinterId() does not compare with KL_ID_BASE"
b = ks.body_of(PRINTERS, "klipperHasPrinters")
assert "klipperAvailable(" in b and "klipperStoreCount(" in b, \
    f"src/{PRINTERS}: klipperHasPrinters() is not klipperAvailable() and a printer in the store"
b = ks.body_of(PRINTERS, "klipperAppendPrinters")
assert "AMS_MAX_PRINTERS" in b and "logSD" in b, \
    f"src/{PRINTERS}: klipperAppendPrinters() does not stop at AMS_MAX_PRINTERS with a line in the log"
b = ks.body_of(PRINTERS, "klipperGetAmsState")
for token in ("moonrakerFetchFacts(", "klipperUnit(", "klipperStateWord(", "AMS_MODEL_KLIPPER"):
    assert token in b, f"src/{PRINTERS}: klipperGetAmsState() does not use {token}"
b = ks.body_of(PRINTERS, "klipperFieldName")
for token in ("KL_LAYOUT_MMU", "KL_LAYOUT_AFC", "KL_LAYOUT_TOOLS"):
    assert token in b, f"src/{PRINTERS}: klipperFieldName() does not tell {token}"

# 5. One assignment: the other Spoolman first, then fresh facts, the plan with
# the clearing ahead of the setting, the read-back, and once more after a second.
where = f"src/{PRINTERS}: klipperAssign()"
b = ks.body_of(PRINTERS, "klipperAssign")
run = need(b, r"\bmoonrakerRunScript\s*\(", where, "never sends a command")
other = need(b, r"\bKT_OTHER_SPOOLMAN\b", where, "does not look for the stored test KT_OTHER_SPOOLMAN")
refuse = need(b, r"\bKL_E_OTHER_SPOOLMAN\b", where, "does not answer KL_E_OTHER_SPOOLMAN")
assert other < run and refuse < run, f"{where}: a command can be sent before another Spoolman is ruled out"
fetch = need(b, r"\bmoonrakerFetchFacts\s*\(", where, "does not fetch the facts")
plan = need(b, r"\bklipperPlanAssign\s*\(", where, "does not plan the assignment")
assert other < fetch < plan < run, f"{where}: the order is not other Spoolman, facts, plan, command"
# R30, R36: only a printer whose stored test is KT_OK passes. KT_OTHER_SPOOLMAN
# keeps its own answer above; every other verdict (KT_NONE, and a printer saved
# while it was off: KT_UNREACHABLE, KT_ACCESS, KT_KLIPPY, KT_NO_SPOOLMAN,
# KT_SM_OFFLINE, KT_BAD_ANSWER, which stop the probe before the Spoolman
# comparison) is refused with KL_E_NOT_TESTED, before any request (the facts
# are one) and so before any command. The gate is "not KT_OK", not a list of
# the refused verdicts: a list lets the next verdict added to the enum through.
# A bay number that is cut to a byte (256 is bay 0) is refused before the
# printer is asked too.
untested = need(b, r"!=\s*KT_OK\b", where, "does not refuse every stored verdict but KT_OK")
refuse_untested = need(b, r"\bKL_E_NOT_TESTED\b", where, "does not answer KL_E_NOT_TESTED")
assert untested < fetch and refuse_untested < fetch and untested < run and refuse_untested < run, \
    f"{where}: a printer without a passed test is asked something or sent a command before it is refused"
assert other < untested and refuse < untested, \
    f"{where}: KL_E_OTHER_SPOOLMAN is decided after the general refusal, which would turn it into KL_E_NOT_TESTED"
assert not re.search(r"==\s*KT_NONE\b", b), \
    f"{where}: the refusal names KT_NONE alone: a printer saved while it was off passes it (R36)"
bay_range = need(b, r"ams_id\s*>\s*255", where, "does not range-check the bay number")
assert bay_range < fetch, f"{where}: the bay number is range-checked after the printer was read"
need(b, r"\bklipperBayField\s*\(", where, "does not turn the bay into a field")
need(b, r"\bKL_E_NO_FIELD\b", where, "does not answer KL_E_NO_FIELD for a bay that is not there")
need(b, r"\bKL_E_NO_PRINTER\b", where, "does not answer KL_E_NO_PRINTER for a printer that is not there")
clear = need(b, r"(?:\.|->)clear\b", where, "does not use the plan's clearing commands")
setting = need(b, r"(?:\.|->)set\b", where, "does not use the plan's setting commands")
assert clear < setting, f"{where}: the spool is set before it is cleared elsewhere"
set_active = re.search(r"\bmoonrakerSetActiveSpool\s*\(", b)
assert set_active, f"{where}: the active spool is never set"
assert "set_active" in b[:set_active.start()], \
    f"{where}: the active spool is set without asking the plan's set_active"
assert set_active.start() > run, f"{where}: the active spool is set before the commands"
holds = [m.start() for m in re.finditer(r"\bklipperFieldHolds\s*\(", b)]
assert len(holds) >= 2, f"{where}: klipperFieldHolds() is not called twice (read back, then once more)"
assert holds[0] > set_active.start(), f"{where}: the read-back comes before the commands"
between = b[holds[0]:holds[1]]
delays = ks.calls(between, "delay") + ks.calls(between, "vTaskDelay")
assert delays, f"{where}: no wait between the two read-backs"
arg = delays[0][1][0]
if "1000" not in arg:
    ident = re.search(r"[A-Za-z_]\w*", arg)
    defined = ident and re.search(rf"#define\s+{ident.group(0)}\s+\(?1000\b",
                                  ks.code_of(PRINTERS) + " " + ks.code_of(PRINTERS_H))
    assert defined, f"{where}: the wait between the read-backs is '{arg}', not one second"
assert re.search(r"\bmoonrakerFetchFacts\s*\(", between), f"{where}: the second read-back does not fetch the facts again"
assert "KL_E_NOT_HELD" in b[holds[1]:], f"{where}: a field that does not hold the spool is not answered with KL_E_NOT_HELD"
assert re.search(r"\b200\b", b[holds[0]:]), f"{where}: no 200 once the field holds the spool"
# The location comes last, after the read-back said yes, and only into the log.
# V50: it is no longer backendPatchSpoolLocation() (the Bambuddy proxy carries
# no extra field), but the location unit, which writes the printer's name and
# the remembered old location in one PATCH directly to the scale's Spoolman and
# gives the displaced spool its old location back.
assert not ks.calls(b, "backendPatchSpoolLocation"), \
    f"{where}: the location still goes through backendPatchSpoolLocation(), not directly to the scale's Spoolman (V50)"
unit = ks.calls(b, "klipperLocationAssigned")
assert len(unit) == 1, f"{where}: klipperLocationAssigned() is not called exactly once"
unit_at, unit_args = unit[0]
assert unit_at > b.rfind("klipperFieldHolds(") and unit_at > b.rfind("KL_E_NOT_HELD"), \
    f"{where}: the location unit is called before the read-back proved the spool is held"
assert not re.search(r"\breturn\b[^;]*\bklipperLocationAssigned\s*\(", b), \
    f"{where}: the answer of the location unit is the answer of the assignment"
after_unit = re.search(r"\breturn\b([^;]*);", b[unit_at:])
assert after_unit and after_unit.group(1).strip() == "200", \
    f"{where}: the first return after the location unit is not 'return 200;' (a failed location write must not fail the assignment)"
# Happy Hare in push and pull writes the location itself, and the switch can
# turn it off: the unit is called inside if (klipperWritesLocation(..., klipperLocSwitch())) { }.
gate = re.search(r"\bif\s*\(\s*klipperWritesLocation\s*\([^;{]*?\bklipperLocSwitch\s*\(\s*\)\s*\)\s*\)\s*\{", b)
assert gate, f"{where}: the location is not asked of if (klipperWritesLocation(..., klipperLocSwitch())) {{"
gate_end = ks.cc.match_close(b, gate.end() - 1, ks.cc._BRACES, "{")
assert gate.end() <= unit_at < gate_end, \
    f"{where}: the location unit is called outside the block of klipperWritesLocation() (Happy Hare in push/pull would get a second writer)"
assert len(unit_args) == 3, f"{where}: klipperLocationAssigned() does not take the spool, the displaced spool and the printer's name"
assert unit_args[0] == "spool_id", f"{where}: klipperLocationAssigned() is not given the assigned spool ({unit_args[0]})"
assert re.search(r"\bklipperPrinterName\s*\(|(?:\.|->)name\b", unit_args[2]), \
    f"{where}: the location is not the printer's name ({unit_args[2]})"
# The displaced spool is the one that stood in the target field BEFORE the
# commands, from the facts read before the first moonrakerRunScript(); for the
# one field of a single extruder it is the printer's active spool.
displaced = unit_args[1]
assert re.fullmatch(r"[A-Za-z_]\w*", displaced), \
    f"{where}: the displaced spool is handed as '{displaced}', not as a name that is set from the facts before"
set_at = re.search(rf"\b{displaced}\s*=(?!=)\s*([^;]*);", b)
assert set_at, f"{where}: '{displaced}' (the displaced spool) is not set in klipperAssign()"
assert set_at.start() < run, f"{where}: the displaced spool is read after the first command, not from the facts before"
expr = set_at.group(1)
assert re.search(r"(?:\.|->)before\b", expr) and not re.search(r"(?:\.|->)after\b", expr), \
    f"{where}: the displaced spool does not come from the facts before the commands ({expr})"
assert re.search(r"\.field\s*\[\s*field\s*\]\s*\.\s*spool_id\b", expr), \
    f"{where}: the displaced spool is not the spool of the target field ({expr})"
assert "KL_LAYOUT_SINGLE" in expr and re.search(r"\.active_spool\b", expr), \
    f"{where}: for the one field of a single extruder the displaced spool is not the active spool ({expr})"
sm_loc = b.find("sm_location_name", unit_at)
assert gate.end() <= sm_loc < gate_end, f"{where}: sm_location_name is not kept in step with the location the unit wrote"
assert re.search(r"\bif\s*\(\s*klipperLocationAssigned\s*\(", b), \
    f"{where}: sm_location_name does not follow the answer of klipperLocationAssigned()"

# 6. backend_api.cpp: the switches.
WHERE = f"src/{BACKEND}"
OTHER_WAY = re.compile(r"\b(?:backendMode|spoolmanHasAmsSource|bb[A-Z]\w*|amsSource[A-Z]\w*|filaman[A-Z]\w*)\s*\(")
SWITCHES = (("backendGetAmsState", r"\bklipperGetAmsState\s*\(", "klipperGetAmsState()"),
            ("backendAssignAmsSlot", r"\bklipperAssign\s*\(", "klipperAssign()"),
            ("backendUnassignAmsSlot", r"\bnotSupported\s*\(", "notSupported()"),
            ("backendFindSpoolSlot", r"\bnotSupported\s*\(", "notSupported()"),
            ("backendFindBaySpool", r"\bnotSupported\s*\(", "notSupported()"),
            ("backendFindUnitSpools", r"\bnotSupported\s*\(", "notSupported()"),
            ("backendFindPrinterSpools", r"\bnotSupported\s*\(", "notSupported()"))
for name, answer, answer_text in SWITCHES:
    where = f"{WHERE}: {name}()"
    b = ks.body_of(BACKEND, name)
    guard = re.search(r"\bklipperIsPrinterId\s*\(\s*printer_id\s*\)", b)
    assert guard, f"{where}: a Klipper number is not told from the others"
    way = OTHER_WAY.search(b)
    assert way is None or guard.start() < way.start(), \
        f"{where}: asks {way.group(0)} before it asks klipperIsPrinterId()"
    later = OTHER_WAY.search(b, guard.end())
    assert re.search(answer, b[guard.end():later.start() if later else len(b)]), \
        f"{where}: the Klipper branch does not answer with {answer_text}"

for name in ("backendHasAmsView", "backendCanAssignAmsSlot"):
    assert "klipperHasPrinters(" in ks.body_of(BACKEND, name), f"{WHERE}: {name}() ignores the Klipper printers"

where = f"{WHERE}: backendListPrinters()"
b = ks.body_of(BACKEND, "backendListPrinters")
append = need(b, r"\bklipperAppendPrinters\s*\(", where, "does not add the Klipper printers")
lists = [m for m in re.finditer(r"\b(?:bbListPrinters|filamanListPrinters|amsSourceListPrinters)\s*\(", b)]
assert lists, f"{where}: asks no server for its printers any more"
for m in lists:
    assert not b[:m.start()].rstrip().endswith("return"), \
        f"{where}: returns the answer of {m.group(0)} straight away, before the Klipper printers are added"
assert append > lists[0].start(), f"{where}: the Klipper printers are added before the other list is asked"
assert "return" not in b[lists[-1].start():append], \
    f"{where}: there is a return between the other list and the Klipper printers: a failed list would skip them"
assert re.search(r"\b200\b", b[append:]), f"{where}: no 200 when only Klipper printers are there"

# 7. V50: the old location in Spoolman (services/klipper_location.cpp). The
# rules are host-tested (test_klipper_location_rules.py); here the device part:
# where it writes, the one retry after a 400, and the fallback.
WHERE = f"src/{LOCATION}"
loc_h = ks.code_of(LOCATION_H)
assert re.search(r"\bbool\s+klipperLocationAssigned\s*\(\s*int\s+\w+\s*,\s*int\s+\w+\s*,\s*const\s+char\s*\*\s*\w+\s*\)", loc_h), \
    f"src/{LOCATION_H}: bool klipperLocationAssigned(int, int, const char*) is not declared"
rules_h = ks.code_of(LOC_RULES_H)
for name in ("klipperPrevToRemember", "klipperRestoreFor", "klipperExtraText", "klipperExtraValue"):
    assert re.search(rf"\b{name}\s*\(", rules_h), f"src/{LOC_RULES_H}: {name}() is not declared"
assert re.search(r'#define\s+KL_PREV_LOC_KEY\s+"scale_prev_location"', ks.source_of(LOC_RULES_H).code_str), \
    f"src/{LOC_RULES_H}: KL_PREV_LOC_KEY is not \"scale_prev_location\""

loc_src = ks.source_of(LOCATION)
loc_code = ks.collapse(loc_src.code)
loc_text = ks.collapse(loc_src.code_str)
# Directly to the scale's Spoolman, in both modes: never the backend's location
# write (in Bambuddy mode that is the proxy, which drops the extra field), never Bambuddy.
base = re.search(r"\b(\w+)\s*=\s*klipperScaleSpoolman\s*\(\s*\)", loc_code)
assert base, f"{WHERE}: the Spoolman address is not taken from klipperScaleSpoolman()"
for banned in ("backendPatchSpoolLocation", "cfg_spoolman_base", "backendBaseUrl", "backendMode"):
    assert not re.search(rf"\b{banned}\b", loc_code), f"{WHERE}: uses {banned} (the location goes directly to klipperScaleSpoolman())"
assert not re.search(r"\bbb[A-Z]\w*\s*\(", loc_code), f"{WHERE}: calls Bambuddy"
SM_CALLS = ("spoolmanGetJson", "spoolmanPostJson", "spoolmanPatchSpool", "spoolmanPatchSpoolLocation")
sent = 0
for name in SM_CALLS:
    for _, args in ks.calls(loc_code, name):
        sent += 1
        assert args and args[0] == base.group(1), \
            f"{WHERE}: {name}() goes to '{args[0] if args else ''}', not to the address of klipperScaleSpoolman() ('{base.group(1)}')"
assert sent >= 4, f"{WHERE}: fewer Spoolman requests than read, PATCH, field and fallback"
assert ks.calls(loc_code, "spoolmanGetJson"), f"{WHERE}: the spool is not read from Spoolman before it is written"
assert ks.calls(loc_code, "spoolmanPatchSpoolLocation"), f"{WHERE}: no fallback that writes the location alone"
for token in ("klipperPrevToRemember(", "klipperRestoreFor(", "klipperExtraText(", "klipperExtraValue(",
              "klipperStoreAt(", "klipperStoreCount(", "KL_PREV_LOC_KEY", "KR_RESTORE", "KR_CLEAR"):
    assert token in loc_code, f"{WHERE}: does not use {token}"
assert re.search(r"\bdisplaced_id\s*>\s*0\b", loc_code) and re.search(r"\bdisplaced_id\s*!=\s*spool_id\b", loc_code), \
    f"{WHERE}: the displaced spool is not checked to be > 0 and another than the assigned one"
assert "SpiRamAllocator" in loc_code or "MALLOC_CAP_SPIRAM" in loc_code or "moonrakerAlloc(" in loc_code, \
    f"{WHERE}: the JSON documents are not put in the PSRAM"
# Location and remembered location go in ONE PATCH: the body that carries the
# location carries the extra field too.
loc_lits = ks.literals_of(LOCATION)
for lit in ("location", "extra"):
    assert lit in loc_lits, f"{WHERE}: the PATCH body has no \"{lit}\""
funcs = ks.cc.find_functions(loc_src.code)
bodies = {n: loc_src.code_str[a + 1:z] for n, a, z in funcs}
builders = [n for n, body in bodies.items() if '"location"' in body]
assert builders and all('"extra"' in bodies[n] and "KL_PREV_LOC_KEY" in bodies[n] for n in builders), \
    f"{WHERE}: a body with the location is built without the extra field ({builders})"
# A 400 (Spoolman does not know the field) makes the field with a POST of
# /api/v1/field/spool/scale_prev_location and tries the PATCH exactly once more.
retry = [n for n, a, z in funcs if re.search(r"\b400\b", loc_src.code[a:z])]
assert len(retry) == 1, f"{WHERE}: not exactly one function that answers a 400 ({retry})"
rb = ks.body_of(LOCATION, retry[0], strings=True)
rwhere = f"{WHERE}: {retry[0]}()"
patches = ks.calls(rb, "spoolmanPatchSpool")
assert len(patches) == 2, f"{rwhere}: not one PATCH and exactly one retry ({len(patches)} PATCH calls)"
assert not re.search(r"\b(?:for|while|do|goto)\b", ks.body_of(LOCATION, retry[0])), f"{rwhere}: retries in a loop"
at400 = re.search(r"\b400\b", rb).start()
assert patches[0][0] < at400 < patches[1][0], f"{rwhere}: the retry is not made on the answer 400"
between = rb[at400:patches[1][0]]
posts = [args for _, args in ks.calls(between, "spoolmanPostJson")]
for n in bodies:
    if n != retry[0] and re.search(rf"\b{n}\s*\(", between):
        posts += [args for _, args in ks.calls(ks.body_of(LOCATION, n, strings=True), "spoolmanPostJson")]
FIELD_PATH = r'"/api/v1/field/spool/"\s*KL_PREV_LOC_KEY|"/api/v1/field/spool/scale_prev_location"'
assert any(len(a) > 1 and re.fullmatch(FIELD_PATH, a[1]) for a in posts), \
    f"{rwhere}: the 400 does not lead to a POST of /api/v1/field/spool/scale_prev_location before the retry"
assert re.search(r"\b20[01]\b", between), f"{rwhere}: the retry does not wait for the field to be made (200/201)"
# The field: text, named with the scale's text in the language that is set.
assert "T(STR_KL_PREV_LOC_FIELD)" in ks.squeeze(loc_code), f"{WHERE}: the field is not named T(STR_KL_PREV_LOC_FIELD)"
assert "field_type" in loc_lits and "text" in loc_lits, f"{WHERE}: the field is not made as field_type \"text\""
rows = ks.lang_rows()
ks.assert_row_complete(rows, "STR_KL_PREV_LOC_FIELD", "src/lang.cpp")
assert ks.enum_has("STR_KL_PREV_LOC_FIELD"), "src/lang.h: STR_KL_PREV_LOC_FIELD is not in the enum"
assert rows["STR_KL_PREV_LOC_FIELD"][:2] == ["Lagerort vor dem Drucker", "Location before the printer"], \
    f"src/lang.cpp: STR_KL_PREV_LOC_FIELD is {rows['STR_KL_PREV_LOC_FIELD'][:2]}"
# The answers of the writes go to the log (the unit logs them now, not
# klipperAssign()), and every line of the log is a KLIPPER: line.
assert any(args and "HTTP %d" in args[0] for _, args in ks.calls(loc_text, "logSDf")), \
    f"{WHERE}: the answer of a location write is not logged"
for fn in ("logSDf", "logSD"):
    for _, args in ks.calls(loc_text, fn):
        assert args and re.match(r'"(?:\[verbose\] )?KLIPPER: ', args[0]), f"{WHERE}: a log line is not a 'KLIPPER: ' line ({args[0] if args else ''})"

print("test_klipper_wiring: ok")
