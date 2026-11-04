"""Read the source tree without a compiler and check that the areas the firmware keeps in the data partition do not overlap: the log ring, the Bambu catalog, the tag family memory and the stored index of "Neu aus Datenbank". Each starts on a flash sector and ends inside the partition of partitions.csv.

Until v0.8.6 the stored index started at FLASH_LOG_BYTES + BAMBU_CATALOG_BYTES, the very place of the tag family memory (tag_family.cpp TAG_FAMILY_OFFSET), and each overwrote the other once Creality or Snapmaker tags were on (Punkteliste n69). The original separated them in v0.8.4-beta.50 (Niko11111, 74645c3); this test keeps any later area from landing on another."""
import re
from pathlib import Path

import klipper_src as ks

ks.assert_ci_step("test_flash_layout.py")

root = Path(__file__).parents[1]
src = root / "src"
SECTOR = 4096

# Each area: the file that owns it, the macro of its offset and of its size.
# The log ring starts at 0 and owns its whole size (flash_log.cpp).
AREAS = [
    ("log ring",            "hardware/flash_log.cpp",        None,                "FLASH_LOG_BYTES"),
    ("Bambu catalog",       "bambu/bambu_catalog.cpp",       "CAT_OFFSET",        "BAMBU_CATALOG_BYTES"),
    ("tag family memory",   "services/tag_family.cpp",       "TAG_FAMILY_OFFSET", "TAG_FAMILY_BYTES"),
    ("stored index",        "services/filament_db_store.cpp", "STORE_OFFSET",     "STORE_BYTES"),
]

DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)\s+(.+?)\s*(?://.*)?$")


def defines():
    """Every #define of src/ whose value is plain arithmetic, by name. A name
    defined twice (a host test may shrink TAG_FAMILY_BYTES under #ifndef) keeps
    its first, the one the firmware builds with."""
    out = {}
    for path in sorted(src.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = DEFINE.match(line)
            if m and m.group(1) not in out:
                out[m.group(1)] = m.group(2)
    return out


DEFS = defines()


def value(name, depth=0):
    assert depth < 10, f"{name}: macros nest too deep"
    expr = DEFS.get(name)
    assert expr is not None, f"#define {name} not found under src/"
    expr = re.sub(r"(\d+)U?L{0,2}\b", r"\1", expr)
    expr = re.sub(r"\b([A-Z_][A-Z0-9_]*)\b", lambda m: str(value(m.group(1), depth + 1)), expr)
    assert re.fullmatch(r"[\d\s()+*/-]+", expr), f"{name} = {DEFS[name]}: not plain arithmetic"
    return int(eval(expr))  # noqa: S307 - digits and operators only, checked above


def partition_size():
    text = (root / "partitions.csv").read_text(encoding="utf-8")
    for line in text.splitlines():
        cells = [c.strip() for c in line.split(",")]
        if len(cells) >= 5 and cells[1] == "data" and cells[2] == "spiffs":
            return int(cells[4], 0)
    raise AssertionError("partitions.csv: no data partition of subtype spiffs")


problems = []
spans = []
for what, rel, off_name, size_name in AREAS:
    # The offset macro has to be the one the owning file uses.
    if off_name and off_name not in ks.code_of(rel):
        problems.append(f"src/{rel}: does not use {off_name}")
    start = value(off_name) if off_name else 0
    size = value(size_name)
    spans.append((start, start + size, what))
    if start % SECTOR:
        problems.append(f"{what}: starts at {start}, not on a {SECTOR} byte sector")

end_of_part = partition_size()
for start, end, what in spans:
    if end > end_of_part:
        problems.append(f"{what}: ends at {end}, past the data partition of {end_of_part} bytes")

spans.sort()
for (a0, a1, a), (b0, b1, b) in zip(spans, spans[1:]):
    if b0 < a1:
        problems.append(f"{a} ({a0}..{a1}) and {b} ({b0}..{b1}) overlap by {a1 - b0} bytes")

assert not problems, "\n".join(problems)
print("ok: " + ", ".join(f"{w} {s // 1024}..{e // 1024} kB" for s, e, w in spans))
