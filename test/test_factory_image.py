"""Check, without a build, the script that looks over the factory image before it is kept: the merged file has to hold the firmware of the same build at 0x10000 and end where it ends, a bootloader has to open it (0xE9 at 0x0) and a partition table has to sit at 0x8000 (0xAA 0x50). A broken image is refused with the text the workflow prints as an error, a missing file is named, and nothing is copied when a check fails."""
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
script = root / "scripts" / "factory_image.py"

if not script.is_file():
    print("factory image: FAILED")
    print("  scripts/factory_image.py is missing")
    sys.exit(1)

# The script is not a package, so it is loaded by its path.
spec = importlib.util.spec_from_file_location("factory_image", script)
factory_image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(factory_image)

errors = []


def expect(name, condition, detail=""):
    if not condition:
        errors.append(f"{name}{': ' + detail if detail else ''}")


def build_factory(firmware):
    """What the platform's merge writes: bootloader at 0x0, partition table at
    0x8000, the firmware at 0x10000 (boot_app0 and the gaps are zeros here)."""
    return b"\xe9" + bytes(0x7FFF) + b"\xaa\x50" + bytes(0x10000 - 0x8002) + firmware


def with_byte(data, index, value):
    out = bytearray(data)
    out[index] = value
    return bytes(out)


def expect_error(name, firmware, factory, phrase, other_phrases=()):
    text = factory_image.check_factory(firmware, factory)
    expect(name, isinstance(text, str) and phrase in text, f"expected '{phrase}', got {text!r}")
    for other in other_phrases:
        expect(name, not (isinstance(text, str) and other in text), f"'{other}' must not appear, got {text!r}")


FW = b"\xe9" + bytes(100)
GOOD = build_factory(FW)
HOLD = "does not hold firmware.bin at 0x10000"
BOOT = "no bootloader at 0x0"
TABLE = "no partition table at 0x8000"

# 1. check_factory: the good image passes, every defect has its own text.
expect("good image", factory_image.check_factory(FW, GOOD) is None, repr(factory_image.check_factory(FW, GOOD)))
expect_error("one byte too many", FW, GOOD + b"\x00", HOLD)
expect_error("one byte too few", FW, GOOD[:-1], HOLD)
expect_error("firmware differs in the last byte", FW, with_byte(GOOD, len(GOOD) - 1, 0x01), HOLD)
expect_error("firmware differs in the first byte", FW, with_byte(GOOD, 0x10000, 0x00), HOLD)
expect_error("firmware of another build", FW + b"\x01", build_factory(FW), HOLD)
expect_error("empty file", FW, b"", HOLD)
# The size is checked on its own: without it an empty firmware would find an empty tail in any file shorter than 0x10000.
expect_error("empty firmware, short file", b"", b"\xe9" + bytes(100), HOLD)
expect_error("bootloader byte is not 0xE9", FW, with_byte(GOOD, 0, 0x00), BOOT, [TABLE, HOLD])
expect_error("partition table byte 1 is not 0xAA", FW, with_byte(GOOD, 0x8000, 0x00), TABLE, [BOOT, HOLD])
expect_error("partition table byte 2 is not 0x50", FW, with_byte(GOOD, 0x8001, 0x00), TABLE, [BOOT, HOLD])
# A firmware without content still has 64 kB in front of it to be checked.
expect("empty firmware, good image", factory_image.check_factory(b"", build_factory(b"")) is None)
expect_error("empty firmware, no bootloader", b"", bytes(0x10000), BOOT)


# 2. The command line the workflows call: python3 scripts/factory_image.py <build_dir> <target>
def run(build_dir, target):
    return subprocess.run(
        [sys.executable, str(script), str(build_dir), str(target)],
        capture_output=True, text=True, errors="replace",
    )


with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    def make_build(name, firmware=None, factory=None):
        build = tmp / name
        build.mkdir()
        if firmware is not None:
            (build / "firmware.bin").write_bytes(firmware)
        if factory is not None:
            (build / "firmware.factory.bin").write_bytes(factory)
        return build

    # 2a. Success: the target is the source, byte for byte, and the sizes are printed.
    build = make_build("good", FW, GOOD)
    target = tmp / "good_target.bin"
    done = run(build, target)
    expect("cli good: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    line = f"firmware.factory.bin: {len(GOOD)} bytes, firmware.bin {len(FW)} bytes at 0x10000"
    expect("cli good: size line", line in done.stdout, done.stdout)
    expect("cli good: no error line", "::error::" not in done.stdout, done.stdout)
    expect("cli good: target exists", target.is_file())
    if target.is_file():
        expect("cli good: target equals the source", target.read_bytes() == GOOD)

    # 2b. The platform did not write the merged file: named, exit 1, nothing copied.
    build = make_build("no_factory", FW, None)
    target = tmp / "no_factory_target.bin"
    done = run(build, target)
    expect("cli no factory file: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli no factory file: error line", "::error::" in done.stdout and "was not written" in done.stdout, done.stdout)
    expect("cli no factory file: names the file", "firmware.factory.bin" in done.stdout, done.stdout)
    expect("cli no factory file: nothing copied", not target.exists())

    # 2c. No firmware.bin either: the same, loudly.
    build = make_build("no_firmware", None, GOOD)
    target = tmp / "no_firmware_target.bin"
    done = run(build, target)
    expect("cli no firmware file: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli no firmware file: error line", "::error::" in done.stdout and "firmware.bin" in done.stdout, done.stdout)
    expect("cli no firmware file: nothing copied", not target.exists())

    # 2d. A broken image: the error text of check_factory behind ::error::, exit 1, nothing copied.
    for name, bad, phrase in [
        ("short", GOOD[:-1], HOLD),
        ("no_bootloader", with_byte(GOOD, 0, 0x00), BOOT),
        ("no_table", with_byte(GOOD, 0x8000, 0x00), TABLE),
    ]:
        build = make_build("bad_" + name, FW, bad)
        target = tmp / f"bad_{name}_target.bin"
        done = run(build, target)
        expect(f"cli {name}: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
        expect(f"cli {name}: error line", "::error::" in done.stdout and phrase in done.stdout, done.stdout)
        expect(f"cli {name}: nothing copied", not target.exists())

    # 2e. A wrong call is not a pass.
    done = subprocess.run([sys.executable, str(script), str(tmp)], capture_output=True, text=True, errors="replace")
    expect("cli without a target: exit 2", done.returncode == 2, f"{done.returncode}: {done.stdout}{done.stderr}")

if errors:
    print("factory image: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("factory image: ok")
