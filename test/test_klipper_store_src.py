"""Read the source tree without a compiler and check how the Klipper printers are kept (services/klipper_store.h): every NVS key fits the 15 characters the memory allows, the list of keys is the one secret and sits with the other secrets of the backup, the printers are loaded at boot next to the AMS source, the load says how full the settings memory is, and no line of the log carries a key."""
import re

import klipper_src as ks

STORE_H = "services/klipper_store.h"
STORE = "services/klipper_store.cpp"

ks.assert_ci_step("test_klipper_store_src.py")

# 1. The NVS keys. Spelled in string literals of the store; the printers' records
# are numbered (kl_p0 .. kl_p63), the other three are single. A key is at most
# 15 characters, the two digits of the highest number included.
literals = [lit for rel in (STORE_H, STORE) for lit in ks.literals_of(rel) if lit.startswith("kl_")]
for single in ("kl_keys", "kl_next", "kl_loc"):
    assert single in literals, f"src/{STORE}: the NVS key '{single}' is not there"
numbered = [lit for lit in literals if lit.startswith("kl_p")]
assert numbered, f"src/{STORE}: no NVS key kl_p<number> for the printers' records"
for lit in literals:
    worst = re.sub(r"%[0-9.]*(?:l|z|ll)?[du]", "63", lit)
    if lit == "kl_p":
        worst = "kl_p63"        # built by appending the digits
    assert 1 <= len(worst) <= 15, f"src/{STORE}: the NVS key '{lit}' is {len(worst)} characters (at most 15)"
for lit in numbered:
    assert lit == "kl_p" or re.fullmatch(r"kl_p%[0-9.]*(?:l|z|ll)?[du]", lit), \
        f"src/{STORE}: '{lit}' is no pattern for a printer's record (kl_p%d)"

# 2. The list of keys is a secret of the backup; the records are not.
backup = ks.source_of("web/backup_rules.cpp").code_str
secret = re.search(r"SECRET_KEYS\s*\[\s*\]\s*=\s*\{(.*?)\};", backup, re.S)
device = re.search(r"DEVICE_KEYS\s*\[\s*\]\s*=\s*\{(.*?)\};", backup, re.S)
assert secret and device, "src/web/backup_rules.cpp: SECRET_KEYS or DEVICE_KEYS is not there"
assert '"kl_keys"' in secret.group(1), "src/web/backup_rules.cpp: 'kl_keys' is not in SECRET_KEYS"
assert "kl_" not in device.group(1), "src/web/backup_rules.cpp: a Klipper key is in DEVICE_KEYS"
assert secret.group(1).count('"kl_') == 1, \
    "src/web/backup_rules.cpp: SECRET_KEYS holds a Klipper key besides 'kl_keys' (the records hold none)"

# 3. Loaded at boot, in the function that loads the AMS source, once.
boot = ks.source_of("app/app_boot.cpp")
assert '#include "services/klipper_store.h"' in boot.text, \
    "src/app/app_boot.cpp: services/klipper_store.h is not included"
spans = [(a, b) for _, a, b in ks.cc.find_functions(boot.code)
         if re.search(r"\bamsSourceLoadSettings\s*\(", boot.code[a:b])]
assert len(spans) == 1, "src/app/app_boot.cpp: amsSourceLoadSettings() is not called in exactly one function"
inside = ks.collapse(boot.code[spans[0][0] + 1:spans[0][1]])
assert len(ks.calls(inside, "klipperStoreLoad")) == 1, \
    "src/app/app_boot.cpp: klipperStoreLoad() is not called once, beside amsSourceLoadSettings()"
assert len(ks.calls(boot.code, "klipperStoreLoad")) == 1, \
    "src/app/app_boot.cpp: klipperStoreLoad() is called more than once"

# 4. The load tells how full the memory is, in the words of the specification.
body = ks.body_of(STORE, "klipperStoreLoad", strings=True)
lines = [args[0] for fn in ("logSDf", "logSD") for _, args in ks.calls(body, fn) if args and args[0].startswith('"NVS: ')]
assert len(lines) == 1, f"src/{STORE}: klipperStoreLoad() does not write exactly one 'NVS: ' line to the log"
assert re.fullmatch(r'"NVS: %\w+ of %\w+ entries used, %\w+ available, %\w+ Klipper printer\(s\)"', lines[0]), \
    f"src/{STORE}: the line is {lines[0]}, expected NVS: <used> of <total> entries used, <available> available, <n> Klipper printer(s)"
whole = ks.source_of(STORE).code
assert re.search(r"\bnvs_get_stats\s*\(", whole), f"src/{STORE}: nvs_get_stats() is not called"
assert "available_entries" in whole, f"src/{STORE}: the free entries (available_entries) are not read"

# 5. No key in the log. The one thing a line may say is that there is one.
found, leaks = ks.log_leaks(STORE)
assert found >= 1, f"src/{STORE}: no log call found, this check would pass on nothing"
assert not leaks, f"src/{STORE}: a log line carries a key: {leaks}"

# 6. What the other tasks call exists in the header.
header = ks.source_of(STORE_H).code
for name in ("klipperStoreLoad", "klipperStoreCount", "klipperStoreAt", "klipperStoreFind",
             "klipperStoreSave", "klipperStoreDelete", "klipperStoreSetTest", "klipperStoreKey",
             "klipperStoreRoomLeft", "klipperLocSwitch", "klipperSetLocSwitch"):
    assert re.search(rf"\b{name}\s*\(", header), f"src/{STORE_H}: {name}() is not declared"
for value in ("KLS_OK", "KLS_BAD_NAME", "KLS_NAME_TAKEN", "KLS_BAD_HOST", "KLS_BAD_PORT",
              "KLS_BAD_KEY", "KLS_FULL", "KLS_NOT_FOUND", "KLS_WRITE_FAILED"):
    assert re.search(rf"\b{value}\b", header), f"src/{STORE_H}: {value} is not in the enum KlipperSave"
assert re.search(r"\bKLS_OK\s*=\s*0\b", header), f"src/{STORE_H}: KLS_OK is not 0"

print("test_klipper_store_src: ok")
