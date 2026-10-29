"""Read the source tree without a compiler and check how the Klipper printers are wired into the AMS backend (services/moonraker_api.cpp, services/klipper_printers.cpp, services/backend_api.cpp): Moonraker is reached with plain http and the key in the header, with only the calls the design names and a POST of the active spool only with a spool; the commands for a printer are worded in one place; the printer list carries the Klipper printers even when Bambuddy does not answer; a Klipper number never reaches a call that is Bambuddy's or the AMS source's; and an assignment is refused for another Spoolman before any command, is read back, and is read back once more."""
import re

import klipper_src as ks

API = "services/moonraker_api.cpp"
API_H = "services/moonraker_api.h"
PRINTERS = "services/klipper_printers.cpp"
PRINTERS_H = "services/klipper_printers.h"
BACKEND = "services/backend_api.cpp"

ks.assert_ci_step("test_klipper_wiring.py")


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

# The key is never written to the log, in any of the files that handle it.
for rel in (API, PRINTERS):
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
        f"the Klipper files call {call}(): Bambuddy gets no new call, only the location through backendPatchSpoolLocation()"

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
# The location comes last and only into the log.
loc = [pos for pos, _ in ks.calls(ks.body_of(PRINTERS, "klipperAssign", strings=True), "backendPatchSpoolLocation")]
assert len(loc) == 1, f"{where}: backendPatchSpoolLocation() is not called exactly once"
text = ks.body_of(PRINTERS, "klipperAssign", strings=True)
patch_args = ks.calls(text, "backendPatchSpoolLocation")[0][1]
assert patch_args[0] == "cfg_spoolman_base", f"{where}: the location is written to {patch_args[0]}, not to the scale's Spoolman (cfg_spoolman_base)"
# The name comes from the printer's record or from klipperPrinterName(), in the
# call or in what the function did before it.
assert re.search(r"\bklipperPrinterName\s*\(|(?:\.|->)name\b", " ".join(patch_args) + " " + text[:loc[0]]), \
    f"{where}: the location is not the printer's name ({patch_args[2]})"
assert loc[0] > text.rfind("klipperFieldHolds("), f"{where}: the location is written before the field is read back"
assert not re.search(r"\breturn\s+backendPatchSpoolLocation", text), f"{where}: the answer of the location write is the answer of the assignment"
assert re.search(r"\bklipperWritesLocation\s*\([^;]*klipperLocSwitch\s*\(\s*\)", text), \
    f"{where}: the location is not asked of klipperWritesLocation(..., klipperLocSwitch())"
assert "logSD" in text[loc[0]:], f"{where}: the answer of the location write is not logged"
assert "sm_location_name" in ks.source_of(PRINTERS).code, f"src/{PRINTERS}: sm_location_name is not kept in step with the location"

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

print("test_klipper_wiring: ok")
