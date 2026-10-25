"""Check, without a build, the script that writes the data files of a release: the version ranking (the same numbers as version_compare.cpp, so the release workflow and the device agree on what is newer), the refusal of a tag that does not rank above what is published, version.json with one entry per board (the WT32 at the top, the Waveshare boards under "boards"), the web flasher manifests, the release title, and the four commands the workflow calls."""
import datetime
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
script = root / "scripts" / "release_files.py"

if not script.is_file():
    print("release files: FAILED")
    print("  scripts/release_files.py is missing")
    sys.exit(1)

# The script is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import release_files as rf  # noqa: E402

errors = []


def expect(name, condition, detail=""):
    if not condition:
        errors.append(f"{name}{': ' + detail if detail else ''}")


def exits_with(name, call, phrase):
    """call() has to end in SystemExit whose text holds phrase."""
    try:
        call()
    except SystemExit as stop:
        expect(name, isinstance(stop.code, str) and phrase in stop.code, f"expected '{phrase}', got {stop.code!r}")
        return
    except Exception as other:  # noqa: BLE001 - any other end is the failure to report
        expect(name, False, f"expected SystemExit, got {other!r}")
        return
    expect(name, False, "expected SystemExit, nothing was raised")


def passes(name, call):
    try:
        call()
    except BaseException as other:  # noqa: BLE001 - SystemExit included: it is the failure here
        expect(name, False, f"must not raise, got {other!r}")


# 1. The constants the workflows and the firmware agree on.
expect("repo", rf.REPO == "Eifel-Joe/SpoolmanScale", repr(rf.REPO))
expect("top board", rf.TOP_BOARD == "wt32-sc01-plus", repr(rf.TOP_BOARD))
expect("other boards", rf.OTHER_BOARDS == ("waveshare-s3-lcd7", "waveshare-s3-lcd7b"), repr(rf.OTHER_BOARDS))
expect("models", rf.MODELS == {
    "wt32-sc01-plus": "WT32-SC01 Plus",
    "waveshare-s3-lcd7": "Waveshare ESP32-S3-Touch-LCD-7",
    "waveshare-s3-lcd7b": "Waveshare ESP32-S3-Touch-LCD-7B",
}, repr(rf.MODELS))

# 2. rank: the order of parseVersion in src/services/version_compare.cpp
#    (major*1e9 + minor*1e6 + patch*1e3 + beta; no "beta" is 999, a bare
#    "-beta" is 0, "-beta.N" with 1..998 is N, anything else 0).
chain = ["v0.8.3-beta.87", "v0.8.4-beta.1", "v0.8.4-beta.2", "v0.8.4", "v0.8.5-beta.1"]
for lower, higher in zip(chain, chain[1:]):
    expect(f"rank {lower} < {higher}", rf.rank(lower) < rf.rank(higher), f"{rf.rank(lower)} vs {rf.rank(higher)}")
# The original's release today (v0.8.3) is below the first pre-release of the fork.
expect("rank v0.8.4-beta.1 > v0.8.3", rf.rank("v0.8.4-beta.1") > rf.rank("v0.8.3"))
# Counter 999 is out of range and ranks like the bare "-beta".
expect("rank beta.999 == beta", rf.rank("v0.8.3-beta.999") == rf.rank("v0.8.3-beta"))
# The history the C++ comment describes: -beta, then its counters, then the next -beta.
expect("rank -beta < -beta.1", rf.rank("v0.5.7-beta") < rf.rank("v0.5.7-beta.1"))
expect("rank -beta.2 < next -beta", rf.rank("v0.5.7-beta.2") < rf.rank("v0.5.8-beta"))
expect("rank beta.998 < final", rf.rank("v0.8.3-beta.998") < rf.rank("v0.8.3"))
# Exact numbers, so that a changed weight cannot hide behind a consistent order.
for tag, number in [
    ("v0.8.4", 8004999),
    ("v0.8.4-beta.1", 8004001),
    ("v0.8.3-beta", 8003000),
    ("v0.8.3-beta.998", 8003998),
    ("v0.8.3-beta.0", 8003000),
    ("v0.8.3-beta.999", 8003000),
    ("v0.8.3-beta.5000", 8003000),
    ("v1.2.3", 1002003999),
    ("1.2.3", 1002003999),
]:
    expect(f"rank {tag}", rf.rank(tag) == number, f"{rf.rank(tag)} != {number}")
# Nothing to read: -1, below every tag.
expect("rank empty", rf.rank("") == -1)
expect("rank None", rf.rank(None) == -1)
expect("rank unreadable", rf.rank("latest") == -1)
expect("rank below every tag", rf.rank("") < rf.rank("v0.0.0-beta"))

# 3. is_prerelease: only a beta counter makes a build internal. The bare
#    "-beta" is a public release and counts as stable.
expect("pre v0.8.4-beta.1", rf.is_prerelease("v0.8.4-beta.1") is True)
expect("pre v0.8.4-beta.87", rf.is_prerelease("v0.8.4-beta.87") is True)
expect("pre v0.8.4", rf.is_prerelease("v0.8.4") is False)
expect("pre v0.8.3-beta", rf.is_prerelease("v0.8.3-beta") is False)

# 4. update_version_json: the WT32 at the top, the other boards under "boards".
URL = "https://github.com/Eifel-Joe/SpoolmanScale/releases"
IMG1 = {"wt32-sc01-plus": ("1" * 64, 3000001), "waveshare-s3-lcd7": ("2" * 64, 3000002), "waveshare-s3-lcd7b": ("3" * 64, 3000003)}
IMG2 = {"wt32-sc01-plus": ("4" * 64, 3100001), "waveshare-s3-lcd7": ("5" * 64, 3100002), "waveshare-s3-lcd7b": ("6" * 64, 3100003)}
IMG3 = {"wt32-sc01-plus": ("7" * 64, 3200001), "waveshare-s3-lcd7": ("8" * 64, 3200002), "waveshare-s3-lcd7b": ("9" * 64, 3200003)}
NODES = ("waveshare-s3-lcd7", "waveshare-s3-lcd7b")


def holds(name, node, kind, version, image):
    """node[kind] is version with the checksum and size of image."""
    sha, size = image
    expect(f"{name}: {kind}", node.get(kind) == version, f"{node.get(kind)!r} != {version!r}")
    expect(f"{name}: {kind}_sha256", node.get(kind + "_sha256") == sha, repr(node.get(kind + "_sha256")))
    expect(f"{name}: {kind}_size", node.get(kind + "_size") == size, repr(node.get(kind + "_size")))


# 4a. The first pre-release into an empty file: prerelease only, no stable.
data1 = rf.update_version_json({}, "v0.8.4-beta.1", IMG1)
expect("first beta: url", data1.get("url") == URL, repr(data1.get("url")))
holds("first beta top", data1, "prerelease", "v0.8.4-beta.1", IMG1["wt32-sc01-plus"])
expect("first beta top: no stable", not data1.get("stable") and not data1.get("stable_sha256") and not data1.get("stable_size"), repr(data1))
for board in NODES:
    node = data1.get("boards", {}).get(board, {})
    holds(f"first beta {board}", node, "prerelease", "v0.8.4-beta.1", IMG1[board])
    expect(f"first beta {board}: no stable", not node.get("stable") and not node.get("stable_sha256") and not node.get("stable_size"), repr(node))
expect("first beta: the top board has no node of its own", "wt32-sc01-plus" not in data1.get("boards", {}), repr(data1.get("boards")))
expect("first beta: only the two other boards", sorted(data1.get("boards", {})) == sorted(NODES), repr(data1.get("boards")))
json.dumps(data1)

# 4b. The release after it: stable and pre-release are the same, with the new checksums.
data2 = rf.update_version_json(data1, "v0.8.4", IMG2)
expect("release: url", data2.get("url") == URL)
for kind in ("stable", "prerelease"):
    holds("release top", data2, kind, "v0.8.4", IMG2["wt32-sc01-plus"])
    for board in NODES:
        holds(f"release {board}", data2["boards"][board], kind, "v0.8.4", IMG2[board])

# 4c. The next pre-release: prerelease moves on, stable stays at v0.8.4 with its own checksum.
data3 = rf.update_version_json(data2, "v0.8.5-beta.1", IMG3)
holds("next beta top prerelease", data3, "prerelease", "v0.8.5-beta.1", IMG3["wt32-sc01-plus"])
holds("next beta top stable", data3, "stable", "v0.8.4", IMG2["wt32-sc01-plus"])
for board in NODES:
    holds(f"next beta {board} prerelease", data3["boards"][board], "prerelease", "v0.8.5-beta.1", IMG3[board])
    holds(f"next beta {board} stable", data3["boards"][board], "stable", "v0.8.4", IMG2[board])

# 4d. The published data is not changed in place (the caller may compare it).
before = json.dumps(data2, sort_keys=True)
rf.update_version_json(data2, "v0.8.5-beta.1", IMG3)
expect("input stays as it was", json.dumps(data2, sort_keys=True) == before)

# 4e. What the file holds beyond that is left alone, and the address is the fork's even when the file says another.
foreign = {
    "url": "https://github.com/Niko11111/SpoolmanScale/releases/latest",
    "note": "kept",
    "boards": {
        "some-other-board": {"stable": "v0.1.0", "stable_sha256": "0" * 64},
        "waveshare-s3-lcd7": {"extra": True},
    },
}
data4 = rf.update_version_json(foreign, "v0.8.4-beta.1", IMG1)
expect("foreign: url is the fork's", data4.get("url") == URL, repr(data4.get("url")))
expect("foreign: top-level key kept", data4.get("note") == "kept")
expect("foreign: unknown board node kept", data4["boards"].get("some-other-board") == foreign["boards"]["some-other-board"], repr(data4["boards"]))
expect("foreign: unknown key in a known node kept", data4["boards"]["waveshare-s3-lcd7"].get("extra") is True, repr(data4["boards"]["waveshare-s3-lcd7"]))
holds("foreign: known node updated", data4["boards"]["waveshare-s3-lcd7"], "prerelease", "v0.8.4-beta.1", IMG1["waveshare-s3-lcd7"])

# 4f. A missing image is no reason to publish half a release.
partial = {board: IMG1[board] for board in ("wt32-sc01-plus", "waveshare-s3-lcd7")}
try:
    rf.update_version_json({}, "v0.8.4-beta.1", partial)
    expect("missing image", False, "nothing was raised for a missing waveshare-s3-lcd7b image")
except KeyError as missing:
    expect("missing image names the board", "waveshare-s3-lcd7b" in str(missing), repr(missing))

# 5. check_newer: the tag has to rank above everything published, on the top level and in every board node.
pub_084 = data2
exits_with("check: same version", lambda: rf.check_newer(pub_084, "v0.8.4"), "does not rank above the published")
exits_with("check: older beta of the same version", lambda: rf.check_newer(pub_084, "v0.8.4-beta.2"), "does not rank above the published")
exits_with("check: older release", lambda: rf.check_newer(pub_084, "v0.8.3"), "does not rank above the published")
passes("check: next beta", lambda: rf.check_newer(pub_084, "v0.8.5-beta.1"))
passes("check: next release", lambda: rf.check_newer(pub_084, "v0.8.5"))
passes("check: nothing published", lambda: rf.check_newer({}, "v0.8.4-beta.1"))
passes("check: empty strings published", lambda: rf.check_newer({"stable": "", "prerelease": None, "boards": {"x": {}}}, "v0.8.4-beta.1"))
# The pre-release counts as published too, not only the stable one.
exits_with("check: same pre-release", lambda: rf.check_newer(data3, "v0.8.5-beta.1"), "does not rank above the published")
exits_with("check: below the pre-release", lambda: rf.check_newer(data3, "v0.8.4-beta.9"), "does not rank above the published")
passes("check: above the pre-release", lambda: rf.check_newer(data3, "v0.8.5-beta.2"))
# A board node can be ahead of the top level; the tag has to clear that one too.
ahead = json.loads(json.dumps(pub_084))
ahead["boards"]["waveshare-s3-lcd7b"]["prerelease"] = "v0.8.6-beta.1"
exits_with("check: node ahead of the top level", lambda: rf.check_newer(ahead, "v0.8.5"), "does not rank above the published")
exits_with("check: node ahead names it", lambda: rf.check_newer(ahead, "v0.8.5"), "v0.8.6-beta.1")
passes("check: above the node", lambda: rf.check_newer(ahead, "v0.8.6-beta.2"))
# A tag that is no version cannot rank above anything.
exits_with("check: empty tag", lambda: rf.check_newer({}, ""), "does not rank above the published")
exits_with("check: unreadable tag", lambda: rf.check_newer(pub_084, "latest"), "does not rank above the published")

# 6. flasher_manifest: one factory file at offset 0 per board.
expected_7b = {
    "name": "SpoolmanScale (Waveshare ESP32-S3-Touch-LCD-7B)",
    "version": "v0.8.4",
    "home_assistant_domain": None,
    "new_install_prompt_erase": True,
    "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": "SpoolmanScale-waveshare-s3-lcd7b_factory.bin", "offset": 0}]}],
}
expect("manifest 7B", rf.flasher_manifest("waveshare-s3-lcd7b", "v0.8.4") == expected_7b, repr(rf.flasher_manifest("waveshare-s3-lcd7b", "v0.8.4")))
wt32 = rf.flasher_manifest("wt32-sc01-plus", "v0.8.4-beta.1")
expect("manifest WT32 name", wt32.get("name") == "SpoolmanScale (WT32-SC01 Plus)", repr(wt32.get("name")))
expect("manifest WT32 file", wt32["builds"][0]["parts"][0]["path"] == "SpoolmanScale-wt32-sc01-plus_factory.bin")
expect("manifest WT32 version", wt32.get("version") == "v0.8.4-beta.1")
expect("manifest is JSON with null", json.loads(json.dumps(wt32))["home_assistant_domain"] is None)

# 7. release_title: the date only behind a pre-release.
expect("title pre-release", rf.release_title("v0.8.4-beta.1", datetime.date(2026, 10, 25)) == "SpoolmanScale v0.8.4-beta.1 (25.10.2026)",
       repr(rf.release_title("v0.8.4-beta.1", datetime.date(2026, 10, 25))))
expect("title release", rf.release_title("v0.8.4", datetime.date(2026, 10, 25)) == "SpoolmanScale v0.8.4",
       repr(rf.release_title("v0.8.4", datetime.date(2026, 10, 25))))
expect("title pads day and month", rf.release_title("v0.8.4-beta.2", datetime.date(2027, 1, 5)) == "SpoolmanScale v0.8.4-beta.2 (05.01.2027)")


# 8. The commands the workflow calls: python3 scripts/release_files.py <command> ...
def run(*args):
    return subprocess.run(
        [sys.executable, str(script), *[str(a) for a in args]],
        capture_output=True, text=True, errors="replace",
    )


with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    # 8a. title: the date of the day the command runs (a midnight between the calls is allowed for).
    done = run("title", "v0.8.4")
    expect("cli title release: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli title release: text", done.stdout.strip() == "SpoolmanScale v0.8.4", repr(done.stdout))
    days = {datetime.date.today()}
    done = run("title", "v0.8.4-beta.1")
    days.add(datetime.date.today())
    allowed = {f"SpoolmanScale v0.8.4-beta.1 ({day.strftime('%d.%m.%Y')})" for day in days}
    expect("cli title pre-release: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli title pre-release: text", done.stdout.strip() in allowed, f"{done.stdout!r} not in {allowed}")

    # 8b. check: exit 0 when the tag is newer, the error line and exit 1 when not, a missing file is "nothing published".
    published = tmp / "published.json"
    published.write_text(json.dumps(pub_084))
    done = run("check", published, "v0.8.5-beta.1")
    expect("cli check newer: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli check newer: no error line", "::error::" not in done.stdout + done.stderr, done.stdout + done.stderr)
    done = run("check", published, "v0.8.4")
    expect("cli check same: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli check same: error line", "::error::" in done.stdout and "does not rank above the published" in done.stdout, done.stdout + done.stderr)
    expect("cli check same: names the published version", "v0.8.4" in done.stdout, done.stdout)
    done = run("check", tmp / "none.json", "v0.8.4-beta.1")
    expect("cli check no file: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    published.write_text("{}")
    done = run("check", published, "v0.8.4-beta.1")
    expect("cli check empty object: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")

    # 8c. version-json: checksum and size from the files of the build.
    images = tmp / "images"
    images.mkdir()
    contents = {
        "wt32-sc01-plus": b"firmware of the wt32",
        "waveshare-s3-lcd7": b"firmware of the waveshare 7 inch, a bit longer",
        "waveshare-s3-lcd7b": b"firmware of the 7B",
    }
    (images / "SpoolmanScale.bin").write_bytes(contents["wt32-sc01-plus"])
    (images / "SpoolmanScale-waveshare-s3-lcd7.bin").write_bytes(contents["waveshare-s3-lcd7"])
    (images / "SpoolmanScale-waveshare-s3-lcd7b.bin").write_bytes(contents["waveshare-s3-lcd7b"])
    real = {board: (hashlib.sha256(data).hexdigest(), len(data)) for board, data in contents.items()}

    out = tmp / "out.json"
    published.write_text(json.dumps(data2))
    done = run("version-json", published, out, "v0.8.5-beta.1", images)
    expect("cli version-json: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli version-json: file written", out.is_file())
    if out.is_file():
        text = out.read_text()
        expect("cli version-json: ends with a newline", text.endswith("\n"))
        expect("cli version-json: same as update_version_json", json.loads(text) == rf.update_version_json(data2, "v0.8.5-beta.1", real), text)
        written = json.loads(text)
        holds("cli version-json top", written, "prerelease", "v0.8.5-beta.1", real["wt32-sc01-plus"])
        holds("cli version-json 7", written["boards"]["waveshare-s3-lcd7"], "prerelease", "v0.8.5-beta.1", real["waveshare-s3-lcd7"])
        holds("cli version-json 7B", written["boards"]["waveshare-s3-lcd7b"], "prerelease", "v0.8.5-beta.1", real["waveshare-s3-lcd7b"])
        holds("cli version-json stable stays", written, "stable", "v0.8.4", IMG2["wt32-sc01-plus"])

    # Nothing published yet: the file is not there, and the result is the same as from an empty object.
    out2 = tmp / "out2.json"
    done = run("version-json", tmp / "none.json", out2, "v0.8.4", images)
    expect("cli version-json no file: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    if out2.is_file():
        expect("cli version-json no file: as from {}", json.loads(out2.read_text()) == rf.update_version_json({}, "v0.8.4", real), out2.read_text())
        expect("cli version-json no file: stable set", json.loads(out2.read_text()).get("stable") == "v0.8.4")
    else:
        expect("cli version-json no file: file written", False)

    # An image the build did not leave: named, exit 1, no half-written result.
    (images / "SpoolmanScale-waveshare-s3-lcd7b.bin").unlink()
    out3 = tmp / "out3.json"
    done = run("version-json", published, out3, "v0.8.5-beta.1", images)
    expect("cli version-json missing image: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
    expect("cli version-json missing image: names the file", "SpoolmanScale-waveshare-s3-lcd7b.bin" in done.stdout + done.stderr, done.stdout + done.stderr)
    expect("cli version-json missing image: nothing written", not out3.exists())

    # A published file that is there but is no JSON object (an error page saved by curl, say) is refused. Reading
    # it as "nothing published" would let the release overwrite what the file held and pass every tag.
    for name, content in [("not json", "<html>404</html>"), ("a list", "[]"), ("null", "null"), ("empty", "")]:
        broken = tmp / "broken.json"
        broken.write_text(content)
        for command, args in [("check", ["v0.8.5"]), ("version-json", [tmp / "out4.json", "v0.8.5", images])]:
            done = run(command, broken, *args)
            expect(f"cli {command}, published file {name}: exit 1", done.returncode == 1, f"{done.returncode}: {done.stdout}{done.stderr}")
            expect(f"cli {command}, published file {name}: error line", "::error::" in done.stdout and "is not a JSON object" in done.stdout, done.stdout + done.stderr)
        expect(f"cli version-json, published file {name}: nothing written", not (tmp / "out4.json").exists())

    # 8d. manifests: one file per board, in a folder that may not exist yet.
    manifests = tmp / "flasher" / "manifests"
    done = run("manifests", manifests, "v0.8.4")
    expect("cli manifests: exit 0", done.returncode == 0, f"{done.returncode}: {done.stdout}{done.stderr}")
    names = sorted(p.name for p in manifests.glob("*")) if manifests.is_dir() else []
    expect("cli manifests: one file per board", names == [
        "manifest-waveshare-s3-lcd7.json", "manifest-waveshare-s3-lcd7b.json", "manifest-wt32-sc01-plus.json"], repr(names))
    for board in rf.MODELS:
        target = manifests / f"manifest-{board}.json"
        if target.is_file():
            text = target.read_text()
            expect(f"cli manifests {board}: content", json.loads(text) == rf.flasher_manifest(board, "v0.8.4"), text)
            expect(f"cli manifests {board}: null, not None", '"home_assistant_domain": null' in text, text)
            expect(f"cli manifests {board}: ends with a newline", text.endswith("\n"))

    # 8e. A wrong call is not a pass.
    for name, args in [
        ("no command", []),
        ("unknown command", ["frobnicate"]),
        ("title without a version", ["title"]),
        ("check without a tag", ["check", published]),
        ("version-json too short", ["version-json", published, tmp / "x.json", "v0.8.4"]),
        ("manifests without a version", ["manifests", tmp / "m"]),
    ]:
        done = run(*args)
        expect(f"cli {name}: exit 2", done.returncode == 2, f"{done.returncode}: {done.stdout}{done.stderr}")

if errors:
    print("release files: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("release files: ok")
