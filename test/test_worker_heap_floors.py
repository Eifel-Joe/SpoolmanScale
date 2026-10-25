"""Read the source tree without a compiler and check that no background worker keeps a heap floor of its own: the ones that talk to a backend take theirs from backendJobMinHeap() and log a refusal through logGateDue(), the list worker asks it for the address the list really goes to, the workers that may reach the Spoolman behind Bambuddy take the higher of two floors, and a refused start and a failed task creation are each logged through a gate of their own."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The only places that may still define a number for a heap floor. The update
# check always speaks https to GitHub and the TLS warm-up runs only for https,
# so both keep their 60000 on purpose; the rules header holds the two floors
# every other worker asks for.
ALLOWED_FLOORS = {
    "services/update_check.cpp",
    "services/backend_http.cpp",
    "services/backend_job_rules.h",
}

# The workers that talk to a backend. Each must ask the rules for its floor
# and gate the log line of a refused start.
WORKERS = [
    "services/backend_job.cpp",
    "app/label_server.cpp",
    "app/label_spool.cpp",
    "services/ams_weights.cpp",
    "services/dried_batch.cpp",
    "services/filament_db.cpp",
    "services/tag_probe_job.cpp",
    "web/web_jobs.cpp",
]
REQUIRED = ["backendJobMinHeap("]

# Workers whose requests can also go straight to the Spoolman server behind a
# BamBuddy (the drying date side channel in dried_batch, the spool read in
# label_spool and ams_weights): the floor is the higher of the backend's and
# that server's address.
BOTH_ADDRESSES = [
    "services/dried_batch.cpp",
    "app/label_spool.cpp",
    "services/ams_weights.cpp",
]
BOTH_CALL = "backendJobMinHeapBoth("

# A line that can repeat on every pass gets a gate of its own: the refused
# start and the failed task creation are two lines, so two gates.
GATE_CALLS_MIN = 2
GATE_CALL = "logGateDue("

FLOOR_DEFINE = re.compile(r"^\s*#define\s+(\w*MIN\w*HEAP\w*)\s+(\d+)")

# The list worker's floor follows the address the list request reads. That is
# backendBaseUrl() in every mode; cfg_spoolman_base is the Spoolman address,
# which is empty in FilaMan and BamBuddy mode and then gave the TLS floor.
LIST_WORKER = "services/backend_job.cpp"
LIST_FLOOR = "backendJobMinHeap(backendBaseUrl())"
SPOOLMAN_FLOOR = re.compile(r"backendJobMinHeap\(\s*cfg_spoolman_base\s*\)")

errors = []

# 1. No file but the three allowed ones defines a fixed floor.
for path in sorted(src.rglob("*")):
    if path.suffix not in (".h", ".cpp", ".hpp", ".c", ".ino"):
        continue
    rel = path.relative_to(src).as_posix()
    if rel in ALLOWED_FLOORS:
        continue
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        match = FLOOR_DEFINE.match(line)
        if match:
            errors.append(f"src/{rel}:{number}: fixed heap floor {match.group(1)} = {match.group(2)}")

# 2. Every worker asks the rules for its floor and gates its refusal line.
for rel in WORKERS:
    path = src / rel
    if not path.is_file():
        errors.append(f"src/{rel}: file is missing")
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    for needle in REQUIRED:
        if needle not in text:
            errors.append(f"src/{rel}: does not call {needle}")
    gates = text.count(GATE_CALL)
    if gates < GATE_CALLS_MIN:
        errors.append(f"src/{rel}: {gates} call(s) of {GATE_CALL}, wants {GATE_CALLS_MIN} "
                      "(one gate for the refused start, one for the failed task creation)")
    if rel in BOTH_ADDRESSES and BOTH_CALL not in text:
        errors.append(f"src/{rel}: does not call {BOTH_CALL} (it may reach the Spoolman behind BamBuddy)")

# 3. The list worker asks for the floor of the backend in use, and no file asks
# for the floor of the Spoolman address.
list_path = src / LIST_WORKER
if list_path.is_file() and LIST_FLOOR not in list_path.read_text(encoding="utf-8", errors="replace"):
    errors.append(f"src/{LIST_WORKER}: does not call {LIST_FLOOR}")
for path in sorted(src.rglob("*")):
    if path.suffix not in (".h", ".cpp", ".hpp", ".c", ".ino"):
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    for number, line in enumerate(text.splitlines(), 1):
        if SPOOLMAN_FLOOR.search(line):
            errors.append(f"src/{path.relative_to(src).as_posix()}:{number}: floor from cfg_spoolman_base, not from the address the request reads")

# 4. Verdict.
if errors:
    print("worker heap floors: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("worker heap floors: ok")
