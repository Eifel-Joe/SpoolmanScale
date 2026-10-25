"""Read the web flasher page (flasher/index.html) as text, without a browser, and check what the release workflow and the visitor rely on: one manifest per board of src/boards and no other, the install button without a manifest and hidden until a board is chosen, the choice setting the manifest, the right claim on each board (only the 7B was tried on a scale), and exactly one line "Firmware vX.Y.Z" for the workflow's sed to set.

The page cannot be tried before the first stable tag publishes it, so this is what holds it together between releases."""
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

root = Path(__file__).parents[1]
page = root / "flasher" / "index.html"
workflow = root / ".github" / "workflows" / "build-all-boards.yml"
boards_dir = root / "src" / "boards"

if not page.is_file():
    print("flasher page: FAILED")
    print("  flasher/index.html is missing")
    sys.exit(1)

# The script is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import release_files as rf  # noqa: E402

text = page.read_text(encoding="utf-8")

errors = []


def expect(name, condition, detail=""):
    if not condition:
        errors.append(f"{name}{': ' + detail if detail else ''}")


# 1. The boards: every BOARD_ID in src/boards has a manifest-<id>.json on the
#    page, and the page names no manifest of a board that does not exist (the
#    workflow publishes manifest-<board>.json for exactly these boards).
BOARD_ID = re.compile(r'^\s*#define\s+BOARD_ID\s+"([^"]+)"', re.MULTILINE)
board_ids = set()
for header in sorted(boards_dir.glob("*/board_config.h")):
    board_ids.update(BOARD_ID.findall(header.read_text(encoding="utf-8", errors="replace")))
expect("src/boards must name three boards", len(board_ids) == 3, f"{sorted(board_ids)}")
for board in sorted(board_ids):
    expect(f"must name manifest-{board}.json", f"manifest-{board}.json" in text)
named = set(re.findall(r"manifest-([A-Za-z0-9_-]+)\.json", text))
expect("must not name a manifest of a board that does not exist", named <= board_ids,
       f"page {sorted(named)}, src/boards {sorted(board_ids)}")
expect('must not use the old single manifest (manifest="manifest.json")',
       re.search(r"""manifest\s*=\s*["']manifest\.json["']""", text) is None)

# 2. The line the workflow's sed sets. The pattern is taken from the workflow
#    itself, so that a change on either side shows up here; the pattern the
#    plan names must find the same single place.
EXPECTED = r"Firmware v[0-9]+\.[0-9]+\.[0-9]+(-beta\.[0-9]+)?"
expect("must hold exactly one place that matches Firmware vX.Y.Z[-beta.N]",
       len(re.findall(EXPECTED, text)) == 1, f"{len(re.findall(EXPECTED, text))} place(s)")
if workflow.is_file():
    sed = re.search(r'sed\s+-E\s+"s/(Firmware [^/]+)/Firmware \$VERSION/"[^\n]*flasher/index\.html',
                    workflow.read_text(encoding="utf-8"))
    expect("the workflow must still set the version in flasher/index.html with sed", sed is not None)
    if sed:
        hits = re.findall(sed.group(1), text)
        expect("the workflow's sed pattern must find exactly one place", len(hits) == 1,
               f"{len(hits)} place(s) for {sed.group(1)}")
else:
    expect("the release workflow must exist", False, "build-all-boards.yml is missing")

# 3. The installer: ESP Web Tools from the same place as the hand-made page, and
#    a link to this fork.
expect("must use the esp-web-install-button element", "esp-web-install-button" in text)
expect("must load ESP Web Tools 10 from unpkg",
       "https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module" in text)
expect("must link to this fork", "https://github.com/Eifel-Joe/SpoolmanScale" in text)
expect("must not link to a branch of the fork that is gone (feat/waveshare-7b)", "feat/waveshare-7b" not in text)


class Page(HTMLParser):
    """The board buttons (with their text), the install button's attributes and
    whether one of its parents is hidden."""

    def __init__(self):
        super().__init__()
        self.stack = []          # (tag, attrs) of the open elements
        self.install = None      # attrs of <esp-web-install-button>
        self.install_hidden = False
        self.boards = {}         # manifest file -> {"name", "text"}
        self._open_board = None  # the manifest file whose button is open

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "esp-web-install-button":
            self.install = attrs
            self.install_hidden = any("hidden" in a for _, a in self.stack)
        if "data-manifest" in attrs:
            self._open_board = attrs["data-manifest"]
            self.boards[self._open_board] = {"attrs": attrs, "text": ""}
        if tag not in ("meta", "link", "br", "img", "input", "hr"):
            self.stack.append((tag, attrs))

    def handle_endtag(self, tag):
        for at in range(len(self.stack) - 1, -1, -1):
            if self.stack[at][0] == tag:
                closed = self.stack[at][1]
                del self.stack[at:]
                if closed.get("data-manifest") == self._open_board:
                    self._open_board = None
                break

    def handle_data(self, data):
        if self._open_board is not None:
            self.boards[self._open_board]["text"] += data


parsed = Page()
parsed.feed(text)

# 4. The choice: one button per board, the install button without a manifest
#    and hidden, and a script that gives the chosen manifest to the button.
expect("must have an esp-web-install-button", parsed.install is not None)
if parsed.install is not None:
    expect("the install button must have no manifest until a board is chosen",
           "manifest" not in parsed.install, f"manifest={parsed.install.get('manifest')!r}")
    expect("the install button must be hidden until a board is chosen", parsed.install_hidden)
expect("must have one choice button per board (data-manifest)",
       sorted(parsed.boards) == sorted(f"manifest-{b}.json" for b in board_ids), f"{sorted(parsed.boards)}")
expect("a choice must set the manifest attribute of the install button",
       re.search(r"""setAttribute\(\s*["']manifest["']""", text) is not None)

# 5. What the visitor is told about each board: its model name and resolution
#    (from release_files.MODELS), and the right claim: only the 7B was tried on
#    a scale, the other two were built and not tried on hardware by this fork.
RESOLUTION = {"wt32-sc01-plus": "480", "waveshare-s3-lcd7": "800", "waveshare-s3-lcd7b": "1024"}
for board in sorted(board_ids):
    entry = parsed.boards.get(f"manifest-{board}.json")
    if entry is None:
        continue  # reported above
    # Plain ASCII for the message: the sign between the numbers is not, and a
    # console that cannot print it must not hide the finding.
    shown = re.sub(r"\s+", " ", entry["text"]).strip().encode("ascii", "replace").decode()
    expect(f"{board}: must show the model name {rf.MODELS[board]!r}", rf.MODELS[board] in shown, shown)
    expect(f"{board}: must show the resolution ({RESOLUTION[board]} wide)", RESOLUTION[board] in shown, shown)
    tried = "tested on the maintainer's scale" in shown.lower() and "not tested" not in shown.lower()
    built = "not tested on hardware by this fork" in shown.lower()
    if board == "waveshare-s3-lcd7b":
        expect(f"{board}: must say it was tested on the maintainer's scale (and not that it was not)", tried, shown)
    else:
        expect(f"{board}: must say it was built, not tested on hardware by this fork", built and not tried, shown)

# 6. The warnings of the hand-made page: an unofficial fork (report here, not at
#    the original) and flashing erases the settings (update through the scale).
flat = re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", text)).lower()
expect("must say this is an unofficial fork", "unofficial fork" in flat)
expect("must name the original project", "https://github.com/niko11111/spoolmanscale" in text.lower())
expect("must say problems are reported at the fork, not at the original", "not at the original" in flat)
expect("must say flashing erases the settings", "erases the scale's settings" in flat)
expect("must say to update through the scale to keep them", "update through the scale" in flat)

# 7. Plain ASCII: the file travels through sed to gh-pages, and U+2014 is
#    banned in this project. Special signs are written as HTML entities.
expect("must not contain U+2014", chr(0x2014) not in text)
expect("must be plain ASCII (write signs as HTML entities)", all(ord(c) < 128 for c in text),
       "first other character: " + next((repr(c) for c in text if ord(c) >= 128), "-"))

if errors:
    print("flasher page: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("flasher page: ok")
