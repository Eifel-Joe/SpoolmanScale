"""Read the source tree without a compiler and check that no background worker keeps a heap floor of its own: the ones that talk to a backend take theirs from backendJobMinHeap() and log a refusal through logGateDue()."""
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
REQUIRED = ["backendJobMinHeap(", "logGateDue("]

FLOOR_DEFINE = re.compile(r"^\s*#define\s+(\w*MIN\w*HEAP\w*)\s+(\d+)")

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

# 3. Verdict.
if errors:
    print("worker heap floors: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("worker heap floors: ok")
