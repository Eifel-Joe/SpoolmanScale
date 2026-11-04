"""Read the source tree without a compiler and check that "Neu aus Datenbank" starts a read of the whole database only with 6000 bytes more internal heap than the other backend jobs (Spec 5.3 T, Punkteliste n68).

On the 7B (v0.8.6-beta.21) the whole database, 3.6 MB from Spoolman, took the internal heap from 48 540 down to 37 084 bytes: 11.5 kB, where the floors of backend_job_rules.h count 6 to 8 kB for the connection of one request. Started right at the https floor (34 kB with the stack in PSRAM) it would have gone to about 22 kB, under the 26 kB of Spec 8 T. So when the index is loaded and no stored index can be used, the floor is the job's floor plus BACKEND_JOB_WHOLE_DB_EXTRA.

The extra applies only where a whole read can come: Spoolman's database in its Spoolman form (directly or behind BamBuddy), not FilaMan, not BamBuddy's colour catalog, not the light form; a form not yet known counts as whole. Whether the stored index can be used is known only once the loop has read its header (fdbStorePrepare()), which happens only after the plain floor let the start through; a start refused for the extra remembers the whole read ahead, so the next tries refuse before reading the flash again."""
import re

import klipper_src as ks

ks.assert_ci_step("test_fdb_whole_reload.py")

RULES = "services/backend_job_rules.h"
FDB = "services/filament_db.cpp"
API = "services/backend_api.cpp"
SMDB = "services/spoolman_filament_db.cpp"
EXTRA = "BACKEND_JOB_WHOLE_DB_EXTRA"

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


def body(rel, name, strings=False):
    try:
        return ks.body_of(rel, name, strings=strings)
    except AssertionError as e:
        problems.append(str(e))
        return ""


# 1. The number lives with the other floors, nowhere else.
rules = ks.source_of(RULES).text
need(re.search(rf"#\s*define\s+{EXTRA}\s+6000\b", rules), f"src/{RULES}: {EXTRA} is not defined as 6000")
for path in sorted(ks.src.rglob("*")):
    if path.suffix not in (".h", ".cpp") or path.relative_to(ks.src).as_posix() == RULES:
        continue
    if re.search(rf"#\s*define\s+{EXTRA}\b", path.read_text(encoding="utf-8", errors="replace")):
        problems.append(f"src/{path.relative_to(ks.src).as_posix()}: defines {EXTRA} of its own")

# 2. startTask(): the extra only for the index, only when a whole read may
# come, known before the flash is read, and found out from it.
start = ks.squeeze(body(FDB, "startTask"))
i_prep = start.find("fdbStorePrepare(")
i_extra = start.find(EXTRA)
need(i_extra >= 0, f"src/{FDB}: startTask() does not add {EXTRA}")
need("backendFdbIndexMayReadWhole()" in start,
     f"src/{FDB}: startTask() does not ask backendFdbIndexMayReadWhole() whether a whole read can come")
need(re.search(r"job==FDB_JOB_INDEX&&backendFdbIndexMayReadWhole\(\)", start),
     f"src/{FDB}: the whole read is asked for other jobs than the index")
need(i_prep < 0 or 0 <= i_extra < i_prep,
     f"src/{FDB}: startTask() does not apply {EXTRA} before fdbStorePrepare() when the whole read is known")
need(start.count(EXTRA) >= 2 and start.rfind(EXTRA) > i_prep >= 0,
     f"src/{FDB}: startTask() does not check {EXTRA} after fdbStorePrepare() found no stored index")
need(re.search(r"=fdbStorePrepare\(s_base\)", start),
     f"src/{FDB}: startTask() does not keep what fdbStorePrepare() found")
need("wholedatabase" in ks.squeeze(ks.body_of(FDB, "startTask", strings=True)).lower(),
     f"src/{FDB}: no log line that names the whole database at the start or its refusal")
# The refusal line, wherever it is written, goes through the postponed line's gate.
fdb_src = ks.source_of(FDB)
refusals = [(name, fdb_src.code_str[a + 1:b]) for name, a, b in ks.cc.find_functions(fdb_src.code)
            if "for the whole database" in fdb_src.code_str[a + 1:b]]
need(refusals, f"src/{FDB}: no refusal line names the whole database")
for name, text in refusals:
    need("logGateDue(&s_start_gate" in ks.squeeze(text),
         f"src/{FDB}: {name}() writes the refusal for the whole database without the postponed line's gate")

# 3. The question mirrors backendFdbLoadIndex(): FilaMan and BamBuddy's colour
# catalog first and never whole, then the Spoolman database.
may = ks.squeeze(body(API, "backendFdbIndexMayReadWhole"))
need("backendIsFilaMan()" in may and "bambuddyHasColorCatalog()" in may,
     f"src/{API}: backendFdbIndexMayReadWhole() does not leave out FilaMan and BamBuddy's colour catalog")
need("spoolmanFdbMayReadWhole(backendFdbBaseUrl())" in may,
     f"src/{API}: backendFdbIndexMayReadWhole() does not ask the Spoolman database at the address the job reads")
need("bambuddyBehindSpoolman()" in may and "BACKEND_SPOOLMAN" in may,
     f"src/{API}: backendFdbIndexMayReadWhole() does not name both ways to a Spoolman database")

# 4. The form as last probed, without a request: the loop asks it.
smw = ks.squeeze(body(SMDB, "spoolmanFdbMayReadWhole"))
need(smw != "", f"src/{SMDB}: spoolmanFdbMayReadWhole() is missing")
need(not re.search(r"spoolmanGet\w*\(|\bprobe\(|spoolmanFdbForm\(", smw),
     f"src/{SMDB}: spoolmanFdbMayReadWhole() sends a request; the loop calls it")
need("SM_FDB_LIGHT" in smw and "SM_FDB_NONE" in smw,
     f"src/{SMDB}: spoolmanFdbMayReadWhole() does not leave out the light form and no database")
need("backendGeneration()" in smw and "s_form_base" in smw,
     f"src/{SMDB}: spoolmanFdbMayReadWhole() takes a form probed for another server or backend")

assert not problems, "\n".join(problems)
print(f"ok: the whole database starts with {EXTRA} more, only where it can come")
