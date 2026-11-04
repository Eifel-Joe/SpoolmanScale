"""Read the source tree without a compiler and check that every request to Spoolman's filament database (/api/v1/external/...) reads its answer within a budget: through fdbReadJson() or the element reader of spoolman_filament_db.cpp, both on FdbBlockStream with an FdbReadLimit (fdb_block_stream.h, lvmiller b1bf6ffb, Spec 5.3 AC f04). spoolmanGetJson() reads an answer to its end however long it is, so no such request may go through it.

f04 bounded the database search of the tag lookup and of the density (spoolman_filament.cpp findInDatabase(), databaseDensity(), materialDensity()); the density by the product name (productDensity(), V34) is ours, came later and read the same search through spoolmanGetJson(). The probe of the database form reads no body and needs no budget."""
import re

import klipper_src as ks

ks.assert_ci_step("test_fdb_read_limit_paths.py")

FILES = ["services/spoolman_filament.cpp", "services/spoolman_filament_db.cpp"]
# A path of the database, spelled out, through the macros of spoolman_filament_db.cpp
# or through its builder of the light form's list.
DB_PATH = re.compile(r'"/api/v1/external/|\bSM_FDB_(?:INDEX|LIST|SEARCH)_PATH\b|\blightListPath\(')
# Only a function that sends a request counts; one that builds a path does not.
SENDS = re.compile(r"\bspoolmanGet\w*\(")
UNBOUNDED = "spoolmanGetJson("
BOUNDED = re.compile(r"\bfdbReadJson\b|\bFdbBlockStream\b|\beachElement\(|\breadIndex\b|\breadEntries\b|\breadEach\b")
# Reads no body: the status code is the answer.
NO_BODY = {"probe", "spoolmanFdbForm"}

problems = []
checked = 0
for rel in FILES:
    source = ks.source_of(rel)
    for name, a, b in ks.cc.find_functions(source.code):
        text = source.code_str[a + 1:b]
        if not DB_PATH.search(text) or not SENDS.search(text) or name in NO_BODY:
            continue
        checked += 1
        code = source.code[a + 1:b]
        if UNBOUNDED in code:
            problems.append(f"src/{rel}: {name}() reads the filament database through spoolmanGetJson(), without a read limit")
        elif not BOUNDED.search(code):
            problems.append(f"src/{rel}: {name}() reads the filament database without fdbReadJson() or a bounded reader")

assert checked >= 6, f"only {checked} function(s) read the filament database: the search went wrong"
assert not problems, "\n".join(problems)
print(f"ok: {checked} functions read the filament database within a budget")
