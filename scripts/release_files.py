#!/usr/bin/env python3
"""The data files of a release: the version ranking, version.json, the web
flasher manifests and the release title, in one place so that one test
(test/test_release_files.py) stands for everything the release workflow does
with them.

    python3 scripts/release_files.py title <version>
    python3 scripts/release_files.py check <published.json> <tag>
    python3 scripts/release_files.py version-json <published.json> <out.json> <version> <dir>
    python3 scripts/release_files.py manifests <outdir> <version>

title        prints the release title; a pre-release carries today's date.
check        exit 1 with an ::error:: line when the tag does not rank above
             everything <published.json> holds, on the top level and in every
             board node. A missing file counts as nothing published yet.
version-json reads <published.json> (missing: nothing published yet), enters
             <version> with the checksum and size of the images in <dir>
             (SpoolmanScale.bin for the WT32, SpoolmanScale-<board>.bin for
             the others) and writes the result to <out.json>.
manifests    writes manifest-<board>.json for every board to <outdir>.

A wrong call exits 2, a published file that is no JSON object or an image the
build did not leave exits 1 with an ::error:: line (the form a GitHub
workflow shows as an annotation). Standard library only, Python 3.9.
"""

from __future__ import annotations

import copy
import datetime
import hashlib
import json
import re
import sys
from pathlib import Path

REPO = "Eifel-Joe/SpoolmanScale"
RELEASES_URL = "https://github.com/" + REPO + "/releases"

# The board whose entry is the top level of version.json and whose image has
# the plain name SpoolmanScale.bin: the fleet and the web flasher knew that
# name before there was a second board. Every other board has the same keys in
# a node of its own under "boards" (where its firmware looks,
# BOARD_VERSION_NODE) and an image named SpoolmanScale-<board>.bin. A new
# board is one more name in OTHER_BOARDS and one more line in MODELS.
TOP_BOARD = "wt32-sc01-plus"
OTHER_BOARDS = ("waveshare-s3-lcd7", "waveshare-s3-lcd7b")
MODELS = {
    "wt32-sc01-plus": "WT32-SC01 Plus",
    "waveshare-s3-lcd7": "Waveshare ESP32-S3-Touch-LCD-7",
    "waveshare-s3-lcd7b": "Waveshare ESP32-S3-Touch-LCD-7B",
}

# How the beta suffix ranks, as in src/services/version_compare.cpp: a tag
# with no "beta" at all is a final release and outranks every beta of its
# X.Y.Z, a bare "-beta" is the public release of that X.Y.Z (0, below its own
# counters), and "-beta.N" counts N when N lies in 1..998. A counter outside
# that range (0, 999 and up) ranks like the bare "-beta": 999 would otherwise
# tie with the final release.
BETA_FINAL = 999
BETA_PUBLIC = 0
BETA_COUNTER_MAX = 998


def rank(v: str) -> int:
    """The number the device compares with >: major*1e9 + minor*1e6 +
    patch*1e3 + beta. -1 for an empty tag or one without X.Y.Z (nothing the
    project releases looks like that, check.sh holds FW_VERSION to
    vX.Y.Z[-beta.N]), so that it ranks below everything."""
    if not v:
        return -1
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", v)
    if not m:
        return -1
    major, minor, patch = (int(g) for g in m.groups())
    beta = BETA_FINAL
    at = v.find("beta")
    if at != -1:
        beta = BETA_PUBLIC
        counter = re.match(r"\.(\d+)", v[at + len("beta"):])
        if counter and 1 <= int(counter.group(1)) <= BETA_COUNTER_MAX:
            beta = int(counter.group(1))
    return major * 10**9 + minor * 10**6 + patch * 10**3 + beta


def is_prerelease(v: str) -> bool:
    """True for an internal build (v0.8.4-beta.1). It only ever lands in
    "prerelease"; a public tag, the bare "-beta" included, updates "stable"
    too, so that devices with pre-releases switched off see it."""
    return re.search(r"-beta\.\d+$", v) is not None


def _published_tags(published: dict) -> list[str]:
    """Every stable and pre-release tag in version.json, on the top level and
    in each board node. A node can be ahead of the top level (a board that was
    released on its own), and the firmware of that board would take that tag
    for the one to beat."""
    nodes = [published]
    boards = published.get("boards")
    if isinstance(boards, dict):
        nodes.extend(boards.values())
    tags = []
    for node in nodes:
        if not isinstance(node, dict):
            continue
        for key in ("stable", "prerelease"):
            value = node.get(key)
            if isinstance(value, str) and value:
                tags.append(value)
    return tags


def check_newer(published: dict, tag: str) -> None:
    """SystemExit unless the tag ranks above everything published. The check
    of the tag against FW_VERSION cannot see a tag that matches but is too
    low, and since the firmware compares itself against version.json such a
    tag would put a red dot on every device and offer it a downgrade."""
    best = max(_published_tags(published), key=rank, default="")
    if rank(tag) <= rank(best):
        raise SystemExit(f"Tag {tag} does not rank above the published {best or '(nothing)'}. "
                         "Every device would be offered a downgrade.")


def _record(node: dict, version: str, image: tuple[str, int]) -> None:
    """Enter version with its checksum and size into one entry of version.json.
    The checksum goes next to the tag: the device sums the download as it
    writes it and refuses to commit on a mismatch, so TLS covers the path and
    this covers the bytes. The size lets a device on the old 3 MB partition
    table offer the web flasher instead of an update that would not fit."""
    sha, size = image
    kinds = ("prerelease",) if is_prerelease(version) else ("prerelease", "stable")
    for kind in kinds:
        node[kind] = version
        node[kind + "_sha256"] = sha
        node[kind + "_size"] = size


def update_version_json(published: dict, version: str, images: dict[str, tuple[str, int]]) -> dict:
    """What version.json says after releasing version. images maps every board
    to (sha256, size) of its image. The published data is not changed in
    place, and what it holds beyond the keys set here stays."""
    data = {"stable": "", "stable_sha256": "", "prerelease": "", "prerelease_sha256": ""}
    data.update(copy.deepcopy(published))
    # Always this fork's address, also over a file the original's release
    # workflow wrote.
    data["url"] = RELEASES_URL
    _record(data, version, images[TOP_BOARD])
    # A "boards" or a board node that is no JSON object is replaced, not
    # refused: a board finds no release in it (versionFilePick() reads keys of
    # an object), and check_newer() skipped it as well (_published_tags).
    boards = data.get("boards")
    if not isinstance(boards, dict):
        boards = data["boards"] = {}
    for board in OTHER_BOARDS:
        node = boards.get(board)
        if not isinstance(node, dict):
            node = boards[board] = {}
        _record(node, version, images[board])
    return data


def flasher_manifest(board: str, version: str) -> dict:
    """The manifest the web flasher reads for one board: the factory image
    (bootloader, partition table and firmware in one file) at offset 0."""
    return {
        "name": "SpoolmanScale (" + MODELS[board] + ")",
        "version": version,
        "home_assistant_domain": None,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": "ESP32-S3",
            "parts": [{"path": "SpoolmanScale-" + board + "_factory.bin", "offset": 0}],
        }],
    }


def release_title(version: str, day: datetime.date) -> str:
    """The title of the GitHub release: the fork's name in every one, because
    the numbers are this fork's own and the original's 0.8.4 is a different
    firmware; the date only behind a pre-release, where the tag alone does not
    say when the build was made."""
    title = "SpoolmanScale (Eifel-Joe fork) " + version
    if is_prerelease(version):
        title += " (" + day.strftime("%d.%m.%Y") + ")"
    return title


def image_name(board: str) -> str:
    """The release asset a board's firmware downloads: the plain
    SpoolmanScale.bin for the WT32, SpoolmanScale-<board>.bin for the others
    (BOARD_OTA_ASSET in each board_config.h under src/boards)."""
    return "SpoolmanScale.bin" if board == TOP_BOARD else "SpoolmanScale-" + board + ".bin"


def _dump(data: dict) -> bytes:
    # Bytes, not text: the file is the same on every machine, whatever the
    # platform makes of "\n" in a text file.
    return (json.dumps(data, indent=2) + "\n").encode("utf-8")


def _load_published(path: Path) -> dict | None:
    """The published version.json, {} when there is none yet. None (after an
    ::error:: line) when the file is there but is no JSON object: an empty
    result would let a release overwrite what the file held."""
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except ValueError:
        data = None
    if not isinstance(data, dict):
        print(f"::error::{path} is not a JSON object, refusing to continue from it")
        return None
    return data


USAGE = """usage: release_files.py title <version>
       release_files.py check <published.json> <tag>
       release_files.py version-json <published.json> <out.json> <version> <dir>
       release_files.py manifests <outdir> <version>"""


def main(argv: list[str]) -> int:
    command, args = (argv[1], argv[2:]) if len(argv) > 1 else ("", [])
    counts = {"title": 1, "check": 2, "version-json": 4, "manifests": 2}
    if counts.get(command) != len(args):
        print(USAGE, file=sys.stderr)
        return 2

    if command == "title":
        print(release_title(args[0], datetime.date.today()))
        return 0

    if command == "check":
        published = _load_published(Path(args[0]))
        if published is None:
            return 1
        tag = args[1]
        try:
            check_newer(published, tag)
        except SystemExit as stop:
            print(f"::error::{stop.code}")
            return 1
        print(f"{tag} ranks above everything published, ok")
        return 0

    if command == "version-json":
        published = _load_published(Path(args[0]))
        if published is None:
            return 1
        out, version, directory = Path(args[1]), args[2], Path(args[3])
        images = {}
        for board in (TOP_BOARD, *OTHER_BOARDS):
            path = directory / image_name(board)
            if not path.is_file():
                print(f"::error::{path} is missing, the build did not leave it")
                return 1
            image = path.read_bytes()
            images[board] = (hashlib.sha256(image).hexdigest(), len(image))
        data = update_version_json(published, version, images)
        out.write_bytes(_dump(data))
        print("version.json:", json.dumps(data))
        return 0

    outdir, version = Path(args[0]), args[1]
    outdir.mkdir(parents=True, exist_ok=True)
    for board in MODELS:
        target = outdir / f"manifest-{board}.json"
        target.write_bytes(_dump(flasher_manifest(board, version)))
        print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
