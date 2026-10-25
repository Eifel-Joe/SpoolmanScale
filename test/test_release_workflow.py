"""Read the release workflow as text, without running it, and check that it publishes for this fork: no own esptool merge (it fails for want of rich_click), nothing written to main (a mirror of the original) and no name of the original's repo; the factory images come from scripts/factory_image.py, the ranking, version.json, the flasher manifests and the release title from scripts/release_files.py; version.json and the flasher go to gh-pages, and only after the GitHub release exists; the boards built besides the matrix one are exactly the boards in src/boards, in the workflow and in release_files.py alike.

The workflow cannot be tried before a real tag (tags are public), so this is what holds it together between releases."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
workflow = root / ".github" / "workflows" / "build-all-boards.yml"
boards_dir = root / "src" / "boards"

if not workflow.is_file():
    print("release workflow: FAILED")
    print("  .github/workflows/build-all-boards.yml is missing")
    sys.exit(1)

# The script is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import release_files as rf  # noqa: E402

text = workflow.read_text(encoding="utf-8")
# What runs, without the comments: a command named only in a comment must not
# count as called. A comment line is one whose first character is "#", in the
# YAML and in the shell blocks alike.
code = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))

errors = []


def expect(name, condition, detail=""):
    if not condition:
        errors.append(f"{name}{': ' + detail if detail else ''}")


# 1. What must not be there at all, comments included.
#    merge-bin: `pio pkg exec ... esptool.py merge-bin` dies with
#    ModuleNotFoundError rich_click; the platform's firmware.factory.bin is used.
#    ci-main and ":main": the original's publishing onto main. Pages of this
#    fork come from gh-pages, main stays a mirror of the original.
FORBIDDEN = [
    ("merge-bin", re.compile(r"merge-bin")),
    ("ci-main", re.compile(r"ci-main")),
    ("push to main (:main)", re.compile(r":main\b")),
    ("the original's repo (Niko11111)", re.compile(r"niko11111", re.IGNORECASE)),
]
for name, pattern in FORBIDDEN:
    for number, line in enumerate(text.splitlines(), 1):
        if pattern.search(line):
            errors.append(f"build-all-boards.yml:{number}: {name}: {line.strip()}")
for number, line in enumerate(code.splitlines(), 1):
    if re.search(r"\bgit\s+push\b.*\bmain\b", line):
        errors.append(f"build-all-boards.yml: pushes to main: {line.strip()}")

# 2. What must run: the factory image check for the matrix board and in the
#    loop over the other boards, and every command of release_files.py the
#    release needs.
factory_calls = len(re.findall(r"scripts/factory_image\.py", code))
expect("must call scripts/factory_image.py for the WT32 and in the board loop", factory_calls >= 2,
       f"{factory_calls} call(s)")
for command in ("check", "version-json", "manifests", "title"):
    pattern = re.compile(r"scripts/release_files\.py[\"']?\s+" + re.escape(command) + r"(?=\s)")
    expect(f"must call scripts/release_files.py {command}", pattern.search(code) is not None)

# 3. gh-pages: the published version.json is the base, the result is pushed
#    there.
expect("must name gh-pages", "gh-pages" in code)
expect("must read the published version.json from origin/gh-pages", "origin/gh-pages:version.json" in code)
expect("must push the pages with git push origin HEAD:gh-pages", re.search(r"git\s+push\s+origin\s+HEAD:gh-pages\b", code) is not None)

# 4. The release before the pages: version.json must never name a tag whose
#    release does not exist (Review Focus 2).
release_step = re.search(r"^\s*-\s*name:\s*Create GitHub Release\s*$", text, re.MULTILINE)
pages_step = re.search(r"^\s*-\s*name:\s*Publish to gh-pages\s*$", text, re.MULTILINE)
expect("must have the step 'Create GitHub Release'", release_step is not None)
expect("must have the step 'Publish to gh-pages'", pages_step is not None)
if release_step and pages_step:
    expect("release step comes before pages", release_step.start() < pages_step.start(),
           f"release at {release_step.start()}, pages at {pages_step.start()}")

# 5. The boards: every BOARD_ID in src/boards, the matrix board on its own and
#    the rest in OTHER_BOARDS, in the workflow and in release_files.py.
BOARD_ID = re.compile(r'^\s*#define\s+BOARD_ID\s+"([^"]+)"', re.MULTILINE)
board_ids = set()
for header in sorted(boards_dir.glob("*/board_config.h")):
    board_ids.update(BOARD_ID.findall(header.read_text(encoding="utf-8", errors="replace")))
expect("src/boards must name the WT32", rf.TOP_BOARD in board_ids, f"{sorted(board_ids)}")
others = sorted(board_ids - {rf.TOP_BOARD})

matrix = re.findall(r"^\s*env:\s*\[([^\]]*)\]", code, re.MULTILINE)
expect("the matrix must build the WT32 alone", [[b.strip() for b in m.split(",")] for m in matrix] == [[rf.TOP_BOARD]],
       f"matrix {matrix}")

lines = re.findall(r"^\s*OTHER_BOARDS:\s*(.*?)\s*$", code, re.MULTILINE)
expect("must have exactly one line OTHER_BOARDS:", len(lines) == 1, f"{len(lines)} line(s)")
for line in lines:
    named = line.split()
    expect("OTHER_BOARDS: must name the boards of src/boards without the WT32", sorted(named) == others
           and len(set(named)) == len(named), f"workflow {named}, src/boards {others}")
expect("must not keep a second list of boards (OTHER_BOARDS=)", re.search(r"\bOTHER_BOARDS=", code) is None)
expect("release_files.OTHER_BOARDS must name the same boards", sorted(rf.OTHER_BOARDS) == others,
       f"release_files {list(rf.OTHER_BOARDS)}, src/boards {others}")

# 6. Verdict.
if errors:
    print("release workflow: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("release workflow: ok")
