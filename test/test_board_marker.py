"""Check, without a build, the script that reads the board line out of a firmware image before a release or a CI run keeps it: every image carries one line "SpoolmanScale-board:<board id>" and a newline (src/services/board_image.cpp), the scale reads along for it when an image is flashed. The script finds every such line in the file (the name at most 40 bytes, the line end behind it) and passes only when the line of the board the file is meant for is there and no other board's line is. A file without a line, with another board's line or with both is refused with the text the workflow prints as an error, a line cut off at the end of the file does not count, a missing file is named, and a wrong call exits 2."""
import importlib.util
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
script = root / "scripts" / "board_marker.py"

if not script.is_file():
    print("board marker: FAILED")
    print("  scripts/board_marker.py is missing")
    sys.exit(1)

# The script is not a package, so it is loaded by its path.
spec = importlib.util.spec_from_file_location("board_marker", script)
board_marker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board_marker)

errors = []


def expect(name, condition, detail=""):
    if not condition:
        errors.append(f"{name}{': ' + detail if detail else ''}")


# The boards of this tree, by their BOARD_ID: the three images must never be
# taken for one another. Two of the names are the start of a third, which is
# exactly where a comparison that stops early would pass the wrong image.
BOARD_ID = re.compile(r'^\s*#define\s+BOARD_ID\s+"([^"]+)"', re.MULTILINE)
board_ids = set()
for header in sorted((root / "src" / "boards").glob("*/board_config.h")):
    board_ids.update(BOARD_ID.findall(header.read_text(encoding="utf-8", errors="replace")))
expect("src/boards must name three boards", len(board_ids) == 3, f"{sorted(board_ids)}")

OWN = "waveshare-s3-lcd7b"
SIBLING = "waveshare-s3-lcd7"
OTHER = "wt32-sc01-plus"


def line(name):
    return b"SpoolmanScale-board:" + name.encode() + b"\n"


def app(*lines):
    """Something like an application image: code-like bytes around the given lines."""
    out = b"\xe9\x03\x02\x2f" + bytes(range(256)).replace(b"\n", b"\x00") * 3
    for each in lines:
        out += b"\x00" + each + b"\x00\x00hello\x00" + bytes(40)
    return out


def factory(*lines):
    """What the factory image looks like: 64 kB of bootloader, table and padding in front of the app."""
    return b"\xe9" + bytes(0x7FFF) + b"\xaa\x50" + bytes(0x10000 - 0x8002) + app(*lines)


def verdict(data, board_id=OWN):
    return board_marker.check_board(data, board_id)


def expect_ok(name, data, board_id=OWN):
    text = verdict(data, board_id)
    expect(name, text is None, f"expected a pass, got {text!r}")


def expect_refused(name, data, phrases=(), board_id=OWN):
    text = verdict(data, board_id)
    expect(name, isinstance(text, str) and text != "", f"expected a refusal, got {text!r}")
    for phrase in phrases:
        expect(name, isinstance(text, str) and phrase in text, f"expected '{phrase}' in {text!r}")


# 1. check_board: the own line passes, everything else is refused.
expect_ok("own line", app(line(OWN)))
expect_ok("own line twice", app(line(OWN), line(OWN)))
expect_ok("own line at the very start", line(OWN) + app())
expect_ok("own line at the very end", app() + line(OWN))
expect_ok("a factory-like file: padding, then the app with the line", factory(line(OWN)))
expect_refused("no line", app(), [OWN])
expect_refused("an empty file", b"", [OWN])
expect_refused("a factory-like file without the line", factory(), [OWN])
expect_refused("another board's line", app(line(OTHER)), [OTHER, OWN])
expect_refused("own line and another board's", app(line(OWN), line(OTHER)), [OTHER])
expect_refused("another board's line first, then the own", app(line(OTHER), line(OWN)), [OTHER])
expect_refused("a factory-like file with another board's line", factory(line(OTHER)), [OTHER])

# 2. The names are compared whole: one that is the start of the other is the
# other board.
expect_refused("the shorter name is not the longer", app(line(SIBLING)), [SIBLING], board_id=OWN)
expect_refused("the longer name is not the shorter", app(line(OWN)), [OWN], board_id=SIBLING)
expect_refused("both siblings in one file", app(line(OWN), line(SIBLING)), [SIBLING])
expect_ok("the shorter name, for the shorter board", app(line(SIBLING)), SIBLING)
for wanted in sorted(board_ids):
    expect_ok(f"{wanted}: its own line", app(line(wanted)), wanted)
    for built in sorted(board_ids - {wanted}):
        expect_refused(f"image of {built} checked as {wanted}", app(line(built)), [built], board_id=wanted)
        expect_refused(f"factory image of {built} checked as {wanted}", factory(line(built)), [built], board_id=wanted)

# 3. What is a line: the prefix, a name of at most 40 bytes and the line end.
# A line cut off at the end of the file is no line, whatever it names.
expect_refused("own line cut off at the end of the file", app() + line(OWN)[:-1], [OWN])
expect_refused("own line cut off in the name", app() + line(OWN)[:-5], [OWN])
expect_refused("only the prefix at the end", app() + b"SpoolmanScale-board:", [OWN])
expect_ok("another board's line cut off at the end does not count", app(line(OWN)) + line(OTHER)[:-1])
name40 = "x" * 40
name41 = "x" * 41
expect_refused("a name of 40 bytes is a line", app(line(name40)), [name40])
expect_ok("a name of 41 bytes is no line", app(line(OWN), line(name41)))
expect_refused("a name of 41 bytes is no line, so no line at all", app(line(name41)), [OWN])
expect_refused("a line with an empty name is another board's", app(line(""), line(OWN)))
# The words of the prefix elsewhere do not make a line.
expect_ok("a similar text in front of the line", app(b"SpoolmanScale-board SpoolmanScale-board;x\n", line(OWN)))
# And the match starts over inside a failed one.
expect_ok("a failed prefix right in front of the line", app(b"SpoolmanScale-board" + line(OWN)))

# 4. board_names: every name, in the order of the file, each time it stands.
expect("names in order", board_marker.board_names(app(line(OTHER), line(OWN), line(OTHER))) == [OTHER, OWN, OTHER],
       repr(board_marker.board_names(app(line(OTHER), line(OWN), line(OTHER)))))
expect("no names", board_marker.board_names(app()) == [], repr(board_marker.board_names(app())))


# 5. The command line the workflows call: python3 scripts/board_marker.py <image> <board_id>
def run(*args):
    return subprocess.run(
        [sys.executable, str(script), *[str(a) for a in args]],
        capture_output=True, text=True, errors="replace",
    )


with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    def write(name, data):
        path = tmp / name
        path.write_bytes(data)
        return path

    # 5a. A pass: one line, the file as it was named, nothing else, exit 0.
    image = write("good.bin", app(line(OWN)))
    done = run(image, OWN)
    expect("cli good: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli good: the line", done.stdout.strip() == f"{image}: board line {OWN} found", done.stdout)
    expect("cli good: no error line", "::error::" not in done.stdout, done.stdout)

    # 5b. A factory image passes the same way.
    image = write("good_factory.bin", factory(line(OWN)))
    done = run(image, OWN)
    expect("cli factory: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli factory: the line", done.stdout.strip() == f"{image}: board line {OWN} found", done.stdout)

    # 5c. Refused: ::error:: with the file, exit 1, and no "found" line.
    for name, data, phrases in [
        ("no_line", app(), [OWN]),
        ("other_board", app(line(OTHER)), [OTHER, OWN]),
        ("both", app(line(OWN), line(OTHER)), [OTHER]),
        ("sibling", app(line(SIBLING)), [SIBLING]),
        ("cut_off", app() + line(OWN)[:-1], [OWN]),
    ]:
        image = write(name + ".bin", data)
        done = run(image, OWN)
        expect(f"cli {name}: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
        expect(f"cli {name}: error line", done.stdout.startswith(f"::error::{image} "), done.stdout)
        expect(f"cli {name}: no found line", f"board line {OWN} found" not in done.stdout, done.stdout)
        for phrase in phrases:
            expect(f"cli {name}: names {phrase}", phrase in done.stdout, done.stdout)

    # 5d. A file that is not there: named, exit 1.
    missing = tmp / "missing.bin"
    done = run(missing, OWN)
    expect("cli missing file: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli missing file: error line", done.stdout.startswith(f"::error::{missing} "), done.stdout)

    # 5e. A wrong call is not a pass, and it is not a refusal of the image either.
    image = write("call.bin", app(line(OWN)))
    for name, args in [
        ("no arguments", []),
        ("no board id", [image]),
        ("a third argument", [image, OWN, OWN]),
        ("an empty board id", [image, ""]),
        ("a board id of 41 bytes", [image, name41]),
        ("a board id with a line end", [image, OWN + "\n"]),
        ("a board id with a blank", [image, "waveshare s3"]),
    ]:
        done = run(*args)
        expect(f"cli {name}: exit 2", done.returncode == 2, f"{done.returncode}: {done.stdout}{done.stderr}")
        expect(f"cli {name}: no pass", f"board line {OWN} found" not in done.stdout, done.stdout)

if errors:
    print("board marker: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("board marker: ok")
