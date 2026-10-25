#!/usr/bin/env python3
"""Read the board line out of a firmware image and refuse the image of another board.

Every image carries the name of the board it was built for as one line of
text, "SpoolmanScale-board:<board id>" and a line end (src/services/
board_image.cpp). The scale reads along for it when an image is flashed, from
the browser upload and from the GitHub download alike, and drops an image of
another board. The release itself needs its own look: version.json carries
the checksum of whatever file the workflow put under a board's name, so a swap
there would pass the checksum and only the scale's own check would stand in
the way.

    python3 scripts/board_marker.py <image> <board_id>

Prints "<image>: board line <board_id> found" and exits 0 when the line of
<board_id> is in the file and no other board's line is. Otherwise it prints
an ::error:: line (the form a GitHub workflow shows as an annotation) and
exits 1. A wrong call exits 2. The same rule is used for the device files
(SpoolmanScale.bin, SpoolmanScale-<board>.bin), the factory images and the
CI build: a factory image holds the application at 0x10000, so the line is
found in it the same way.

What counts as a line is what the scale's scan counts (NAME_MAX in
board_image.cpp): the prefix, a name of at most 40 bytes, the line end. A
line cut off at the end of the file is none, and neither is a name longer than
40 bytes. Names are compared whole, so waveshare-s3-lcd7 is not
waveshare-s3-lcd7b. Any other board's line fails the file even where the own
line is there too: the scale would take that image (the own line settles it),
a release has no reason to carry two. Standard library only, Python 3.9.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

PREFIX = b"SpoolmanScale-board:"
# Longer than any board name is allowed to become, the same limit as the scan
# on the scale.
NAME_MAX = 40
LINE = re.compile(re.escape(PREFIX) + rb"([^\n]{0," + str(NAME_MAX).encode() + rb"})\n")
# A board id as it can stand in a line: printable ASCII, no blank.
BOARD_ID = re.compile(r"[\x21-\x7e]{1,%d}" % NAME_MAX)


def board_names(data: bytes) -> list[str]:
    """The name in every board line of the image, in the order of the file."""
    return [m.group(1).decode("latin-1") for m in LINE.finditer(data)]


def check_board(data: bytes, board_id: str) -> str | None:
    """None when the image is for board_id, otherwise the text to print.

    The text starts with what is wrong and names the board that was expected.
    """
    names = board_names(data)
    others = [n for n in names if n != board_id]
    if others:
        shown = ", ".join(repr(n) for n in dict.fromkeys(others))
        if board_id in names:
            return f"carries a board line of another board besides its own ({shown}), expected only {board_id}"
        return f"is for another board ({shown}), expected {board_id}"
    if not names:
        return f"carries no board line, expected SpoolmanScale-board:{board_id}"
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 3 or BOARD_ID.fullmatch(argv[2]) is None:
        print("usage: board_marker.py <image> <board_id>", file=sys.stderr)
        return 2
    path = Path(argv[1])
    board_id = argv[2]

    if not path.is_file():
        print(f"::error::{argv[1]} is missing, the build did not leave it")
        return 1
    problem = check_board(path.read_bytes(), board_id)
    if problem is not None:
        print(f"::error::{argv[1]} {problem}")
        return 1
    print(f"{argv[1]}: board line {board_id} found")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
