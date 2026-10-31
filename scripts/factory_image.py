#!/usr/bin/env python3
"""Look over the factory image a build leaves behind, then keep a copy of it.

The platform writes <build_dir>/firmware.factory.bin at the end of every
build: bootloader at 0x0, partition table at 0x8000, boot_app0 at 0xe000 and
the firmware at 0x10000, merged for the flash mode, frequency and size of the
board. It only prints a line when that merge goes wrong and does not fail the
build, and a broken image must not reach someone who has no screen to find
out with. So this reads the result before it is kept.

    python3 scripts/factory_image.py <build_dir> <target>

Reads <build_dir>/firmware.bin and <build_dir>/firmware.factory.bin, prints
one line with both sizes and writes the factory image to <target> when it
holds up. Otherwise it prints an ::error:: line (the form a GitHub workflow
shows as an annotation) and exits 1, and <target> is not touched. A wrong
call exits 2. The CI build and the release workflow call it the same way, so
one test (test/test_factory_image.py) stands for both. Standard library only,
Python 3.9.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Where the platform's merge puts the firmware, and the two places that tell
# a merged image from a file that is merely the right length.
FIRMWARE_AT = 0x10000
TABLE_AT = 0x8000
# 0xE9 opens every ESP32 image (the bootloader at 0x0), 0xAA 0x50 opens the
# partition table at 0x8000.
BOOT_MAGIC = b"\xe9"
TABLE_MAGIC = b"\xaa\x50"


def check_factory(firmware: bytes, factory: bytes) -> str | None:
    """None when the factory image is in order, otherwise the text to print.

    The first defect found is the one reported: size and firmware first, then
    the bootloader, then the partition table (the order of the shell step this
    replaces).
    """
    # The file ends where the firmware ends, and what sits at 0x10000 is the
    # firmware just built, byte for byte.
    if len(factory) != len(firmware) + FIRMWARE_AT or factory[FIRMWARE_AT:] != firmware:
        return f"firmware.factory.bin does not hold firmware.bin at {FIRMWARE_AT:#x}"
    if factory[:1] != BOOT_MAGIC:
        return f"firmware.factory.bin has no bootloader at 0x0 (found {factory[:1].hex()}, expected {BOOT_MAGIC.hex()})"
    if factory[TABLE_AT:TABLE_AT + len(TABLE_MAGIC)] != TABLE_MAGIC:
        found = factory[TABLE_AT:TABLE_AT + len(TABLE_MAGIC)].hex()
        return f"firmware.factory.bin has no partition table at {TABLE_AT:#x} (found {found}, expected {TABLE_MAGIC.hex()})"
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: factory_image.py <build_dir> <target>", file=sys.stderr)
        return 2
    build_dir = Path(argv[1])
    target = Path(argv[2])
    firmware_path = build_dir / "firmware.bin"
    factory_path = build_dir / "firmware.factory.bin"

    if not factory_path.is_file():
        print(f"::error::{factory_path} was not written, the platform's merge step failed")
        return 1
    if not firmware_path.is_file():
        print(f"::error::{firmware_path} is missing, the build did not leave it")
        return 1
    firmware = firmware_path.read_bytes()
    factory = factory_path.read_bytes()
    print(f"firmware.factory.bin: {len(factory)} bytes, firmware.bin {len(firmware)} bytes at {FIRMWARE_AT:#x}")

    problem = check_factory(firmware, factory)
    if problem is not None:
        print(f"::error::{problem}")
        return 1

    # What was looked over is what is kept: the bytes just checked are the ones
    # written, not a second read of the file.
    target.write_bytes(factory)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
