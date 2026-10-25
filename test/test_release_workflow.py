"""Read the release workflow as text, without running it, and check that it publishes for this fork: no own esptool merge (it fails for want of rich_click), nothing written to main (a mirror of the original) and no name of the original's repo; the factory images come from scripts/factory_image.py, the ranking, version.json, the flasher manifests and the release title from scripts/release_files.py; version.json and the flasher go to gh-pages, and only after the GitHub release exists; release and pages happen on a tag push only, and a pre-release title carries the German date; the boards built besides the matrix one are exactly the boards in src/boards, in the workflow and in release_files.py alike; one release runs at a time, and the release step fails on a file that is missing. Before anything is released, scripts/board_marker.py reads every image (the device file, the file for the browser upload and the factory image of every board) for the line of the board it is meant for, and the CI build does the same for the firmware.bin of every board: version.json carries the checksum of whatever file sits under a board's name, so a swapped file would otherwise pass the checksum.

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


def step_lines(name):
    """The keys of the step `- name: <name>` as (indent, line) pairs, up to the
    next step or the end of the steps; None when there is no such step."""
    all_lines = text.splitlines()
    for at, line in enumerate(all_lines):
        m = re.match(r"^(\s*)-\s*name:\s*(.*?)\s*$", line)
        if m and m.group(2) == name:
            indent = len(m.group(1))
            block = []
            for follow in all_lines[at + 1:]:
                if follow.strip() and len(follow) - len(follow.lstrip()) <= indent:
                    break
                block.append((len(follow) - len(follow.lstrip()), follow.strip()))
            return indent, block
    return None


# 5. Release and publish on a tag push only. On a manual run github.ref_name
#    is a branch: the release would get the branch's name as its tag while
#    version.json names FW_VERSION, a release that does not exist.
TAG_ONLY = re.compile(r"^if:\s*(?:\$\{\{\s*)?github\.ref_type\s*==\s*'tag'(?:\s*\}\})?$")
for name in ("Create GitHub Release", "Publish to gh-pages"):
    step = step_lines(name)
    if step is None:
        continue  # reported above
    indent, block = step
    expect(f"step '{name}' must run on a tag push only (if: github.ref_type == 'tag')",
           any(depth == indent + 2 and TAG_ONLY.match(line) for depth, line in block))

# 6. The date in a pre-release title is the German one (the runner's clock is
#    UTC): the title step runs with TZ Europe/Berlin.
title_step = step_lines("Release title")
expect("must have the step 'Release title'", title_step is not None)
if title_step is not None:
    expect("step 'Release title' must run with TZ: Europe/Berlin",
           any(re.match(r"^TZ:\s*['\"]?Europe/Berlin['\"]?$", line) for _, line in title_step[1]))

# 7. The boards: every BOARD_ID in src/boards, the matrix board on its own and
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

# 8. One release at a time, and none with a file missing. Two tags pushed close
#    together would otherwise race on gh-pages: both read the same version.json
#    and the second push is refused, or the older tag lands last. So the whole
#    workflow is in one concurrency group, and a run that has started is not
#    cancelled (it may have created its release already). The release step
#    fails when a file it names is not there, instead of publishing a release
#    without the image a device would download.
all_code = code.splitlines()
concurrency = None
for at, line in enumerate(all_code):
    if re.match(r"^concurrency:", line):
        block = [line[len("concurrency:"):]]
        for follow in all_code[at + 1:]:
            if follow.strip() and not follow[0].isspace():
                break
            block.append(follow)
        concurrency = " ".join(block)
        break
expect("must run one release at a time (a workflow-level concurrency:)", concurrency is not None)
if concurrency is not None:
    expect("the concurrency group must be release",
           re.search(r"\bgroup:\s*['\"]?release['\"]?(?=[\s,}]|$)", concurrency) is not None, concurrency.strip())
    expect("a running release must not be cancelled (cancel-in-progress: false)",
           re.search(r"\bcancel-in-progress:\s*false\b", concurrency) is not None, concurrency.strip())
release = step_lines("Create GitHub Release")
if release is not None:
    expect("step 'Create GitHub Release' must fail on a missing file (fail_on_unmatched_files: true)",
           any(re.match(r"^fail_on_unmatched_files:\s*true$", line) for _, line in release[1]))

# 9. The board line of every image, before anything is released. Every image
#    carries "SpoolmanScale-board:<id>", and version.json carries the sha256
#    of whatever file the workflow put under a board's name, so a file under
#    the wrong name would pass the scale's checksum: scripts/board_marker.py
#    reads each file for the line of the board it is meant for. A failing call
#    stops the step (bash -e), and the step stands before the checksums are
#    taken (Update version.json), before the release and before the pages. It
#    runs on a manual run as well: no `if:`. The test fills in the step's
#    variables for every board (the matrix board, and each of OTHER_BOARDS in
#    the loop that names it) and compares the calls with the files the
#    release carries: SpoolmanScale.bin for the WT32 and SpoolmanScale-<id>.bin
#    for the others, the same with the version in the name, and each board's
#    factory image.
MARKER_STEP = "Check that every image names its board"
VERSION = "v0.0.0"
expect("scripts/board_marker.py must exist", (root / "scripts" / "board_marker.py").is_file())
marker_step = step_lines(MARKER_STEP)
expect(f"must have the step '{MARKER_STEP}'", marker_step is not None)


def expected_images():
    """(file, board) of every image the release carries."""
    out = set()
    for board in sorted(board_ids):
        if board == rf.TOP_BOARD:
            out.add(("SpoolmanScale.bin", board))
            out.add((f"SpoolmanScale_{VERSION}.bin", board))
        else:
            out.add((f"SpoolmanScale-{board}.bin", board))
            out.add((f"SpoolmanScale-{board}_{VERSION}.bin", board))
        out.add((f"SpoolmanScale-{board}_{VERSION}_factory.bin", board))
    return out


def fill(line, board=None):
    """The line with the variables of the step replaced: the matrix board, the
    other boards, the version and, inside the loop, the loop's board."""
    line = re.sub(r"\$\{\{\s*matrix\.env\s*\}\}", rf.TOP_BOARD, line)
    line = re.sub(r"\$\{?OTHER_BOARDS\}?", " ".join(others), line)
    line = re.sub(r"\$\{\{\s*steps\.version\.outputs\.version\s*\}\}|\$\{?VERSION\}?", VERSION, line)
    if board is not None:
        line = re.sub(r"\$\{?BOARD\}?", board, line)
    return line


def board_marker_calls(lines):
    """[(file, board, line)] for every call of scripts/board_marker.py in the
    step, a call inside `for BOARD in ...; do` once for every board it names."""
    calls = []
    loop = None
    for line in lines:
        if line.startswith("#"):
            continue
        m = re.match(r"^for\s+BOARD\s+in\s+(.+?)\s*;\s*do$", line)
        if m:
            loop = fill(m.group(1)).split()
            continue
        if line == "done":
            loop = None
            continue
        if "scripts/board_marker.py" not in line:
            continue
        for board in (loop if loop is not None else [None]):
            call = re.search(r"python3\s+scripts/board_marker\.py[\"']?\s+(\S+)\s+(\S+)", fill(line, board))
            if call is None:
                calls.append((None, None, line))
            else:
                calls.append((call.group(1).strip("\"'"), call.group(2).strip("\"'"), line))
    return calls


if marker_step is not None:
    indent, block = marker_step
    calls = board_marker_calls([line for _, line in block])
    for file, board, line in calls:
        expect("every call must read `python3 scripts/board_marker.py <file> <board>`", file is not None, line)
    found = {(file, board) for file, board, _ in calls if file is not None}
    expected = expected_images()
    for file, board in sorted(expected - found):
        expect(f"must check {file} for {board}", False)
    for file, board in sorted(found - expected):
        expect(f"checks {file} for {board}, which is not an image of that board", False)
    for _, _, line in calls:
        expect("a call must not be able to pass on a failure (|| or &)", re.search(r"\|\||&", line) is None, line)
    expect(f"step '{MARKER_STEP}' must run on a manual run as well (no if:)",
           not any(depth == indent + 2 and line.startswith("if:") for depth, line in block))
    expect(f"step '{MARKER_STEP}' must not continue on error",
           not any(re.match(r"^continue-on-error:\s*true\b", line) for _, line in block))
    marker_at = re.search(r"^\s*-\s*name:\s*" + re.escape(MARKER_STEP) + r"\s*$", text, re.MULTILINE)
    build_other = re.search(r"^\s*-\s*name:\s*Build the other boards\s*$", text, re.MULTILINE)
    json_step = re.search(r"^\s*-\s*name:\s*Update version\.json\s*$", text, re.MULTILINE)
    if marker_at and build_other and json_step and release_step and pages_step:
        expect("the board line check must come after the last image is built",
               build_other.start() < marker_at.start())
        expect("the board line check must come before version.json takes the checksums",
               marker_at.start() < json_step.start())
        expect("the board line check must come before the release",
               marker_at.start() < release_step.start())
        expect("the board line check must come before the pages",
               marker_at.start() < pages_step.start())

# 10. CI: every board's firmware.bin is read for its board line on every push,
#     so that a missing or wrong line shows before a release is tried.
ci_path = root / ".github" / "workflows" / "ci.yml"
if not ci_path.is_file():
    errors.append(".github/workflows/ci.yml is missing")
else:
    ci_code = "\n".join(line for line in ci_path.read_text(encoding="utf-8").splitlines()
                        if not line.lstrip().startswith("#"))
    ci_matrix = re.findall(r"^\s*env:\s*\[([^\]]*)\]", ci_code, re.MULTILINE)
    expect("ci.yml must build every board of src/boards",
           [sorted(b.strip() for b in m.split(",")) for m in ci_matrix] == [sorted(board_ids)],
           f"matrix {ci_matrix}, src/boards {sorted(board_ids)}")
    ci_build = re.search(r"^\s*run:\s*pio run -e \$\{\{\s*matrix\.env\s*\}\}\s*$", ci_code, re.MULTILINE)
    ci_check = re.search(r"^\s*run:\s*python3 scripts/board_marker\.py\s+"
                         r"\.pio/build/\$\{\{\s*matrix\.env\s*\}\}/firmware\.bin\s+\$\{\{\s*matrix\.env\s*\}\}\s*$",
                         ci_code, re.MULTILINE)
    expect("ci.yml must run `python3 scripts/board_marker.py .pio/build/${{ matrix.env }}/firmware.bin ${{ matrix.env }}`",
           ci_check is not None)
    expect("ci.yml must have the build step `pio run -e ${{ matrix.env }}`", ci_build is not None)
    if ci_build and ci_check:
        expect("the board line check in ci.yml must come after the build", ci_build.start() < ci_check.start())

# 11. Verdict.
if errors:
    print("release workflow: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("release workflow: ok")
