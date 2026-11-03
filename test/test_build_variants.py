"""Read the source tree and the build files as text, without a compiler, and check that the acceptance and documentation builds stay what they are meant to be: switches, off in every release. SSC_ACCEPTANCE (the acceptance firmware: touch the display and simulate a weight from the web) and SSC_DOC_BUILD (the documentation firmware: the same, plus it writes nothing to any server, printer or tag and shows the release version) are set by one build environment each (platformio.ini) and by nothing else; app_config.h derives SSC_ACCEPTANCE from SSC_DOC_BUILD and builds the version suffix.

What the switches switch is read the way the preprocessor reads it, as far as `defined(...)` goes: each file is read three times, for a release (no switch), for the acceptance firmware (-DSSC_ACCEPTANCE) and for the documentation firmware (-DSSC_DOC_BUILD alone), following the project's own #include "..." lines, and the code that is left is what is checked: the files of the two builds are empty in a release, their header stubs take over, the write lock of the documentation build is not there in the acceptance firmware (it would refuse every send of an acceptance), and a release carries no route of the acceptance build. The routes /api/touch and /api/sim/weight are spelled in src/web/web_doc.cpp and nowhere else in src/, comments included, so that the text search of the release run needs no exceptions.

The build files are read for the same purpose: platformio.ini sets a switch in the two environments waveshare-s3-lcd7b-abnahme (-DSSC_ACCEPTANCE) and waveshare-s3-lcd7b-doku (-DSSC_DOC_BUILD alone), both extending the 7B's, and in no other section; neither workflow's build matrix nor the release workflow's list of boards names them; ci.yml builds the acceptance firmware in a job of its own for a push to a work branch (not dev, not main, not a pull request), proves that firmware.bin holds /api/touch before it uploads it for one day, and builds the documentation firmware in a job that runs on request only. The `if:` of those jobs is evaluated for a push, a pull request and a manual run, and the proof step is run with bash against sample files.

The rules (services/doc_build_rules.h) have their own test with a compiler, test_doc_build_rules.py; the version ranking has test_version_compare.py; what the release run does with these texts has test_release_workflow.py."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer and the function finder of the convention ratchet (comments
# blanked, string literals kept where asked). The script is not a package,
# so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

errors = []


def expect(name, condition, detail=""):
    if not condition:
        errors.append(f"{name}{': ' + detail if detail else ''}")


def collapse(text):
    """Runs of blanks as one blank, so that a declaration is found however it is wrapped."""
    return re.sub(r"\s+", " ", text).strip()


# ---------------------------------------------------------------- reading files

_sources = {}


def source_of(rel):
    """src/<rel> as the ratchet's Source; None (and an error, once) when the
    file is missing."""
    if rel not in _sources:
        path = src / rel
        if path.is_file():
            _sources[rel] = cc.Source(path.read_text(encoding="utf-8", errors="replace"))
        else:
            _sources[rel] = None
            errors.append(f"src/{rel}: the file is missing")
    return _sources[rel]


def code_lines(rel):
    """[(line number, stripped line)] of the file's code without comments and
    without blank lines; string literals kept."""
    source = source_of(rel)
    if source is None:
        return []
    return [(number, line.strip()) for number, line in enumerate(source.code_str.split("\n"), 1) if line.strip()]


def body_of(rel, name):
    """The text between the braces of the one function called name, blanks
    collapsed, string literals kept; None (and an error) when there is not
    exactly one such function."""
    source = source_of(rel)
    if source is None:
        return None
    spans = [(a, b) for found, a, b in cc.find_functions(source.code) if found == name]
    if len(spans) != 1:
        errors.append(f"src/{rel}: {name}() is not defined exactly once ({len(spans)} found)")
        return None
    return collapse(source.code_str[spans[0][0] + 1:spans[0][1]])


# ----------------------------------------------------- the preprocessor, a little

DIRECTIVE = re.compile(r"^\s*#\s*(\w+)\s*(.*)$")


class Unit:
    """One translation unit read as far as the switches go. The macros begin
    with the ones the build environment sets (-DNAME is NAME as 1). The file is
    read line by line: #if, #ifdef, #ifndef, #elif, #else and #endif decide
    which lines are in, #define and #undef change the macros, a quoted
    #include of a file under src/ is read in place (once per unit, like a
    header with #pragma once), anything else is ignored. A condition is read
    when it consists of defined(NAME), numbers, macros with a number as value,
    !, &&, || and brackets; any other is reported in `problems`, for the file
    that is read first, and counts as false."""

    def __init__(self, switches):
        self.macros = {name: "1" for name in switches}
        self.seen = set()
        self.visible = {}      # rel -> the code lines that are in, stripped
        self.problems = []

    def condition(self, text, where):
        expr = re.sub(r"\bdefined\s*\(\s*(\w+)\s*\)|\bdefined\s+(\w+)",
                      lambda m: " 1 " if (m.group(1) or m.group(2)) in self.macros else " 0 ", text)
        expr = expr.replace("&&", " and ").replace("||", " or ")
        expr = re.sub(r"!(?!=)", " not ", expr)

        def name(m):
            word = m.group(0)
            if word in ("and", "or", "not"):
                return word
            value = self.macros.get(word, "0").strip()
            number = re.fullmatch(r"(-?\d+)[uUlL]*", value)
            return number.group(1) if number else "0"

        expr = re.sub(r"(?<!\w)[A-Za-z_]\w*", name, expr)
        if re.search(r"[^\sandort\d()<>=!+\-]", expr):
            self.problems.append(f"{where}: cannot read the condition `{text.strip()}`")
            return False
        try:
            return bool(eval(expr, {"__builtins__": {}}, {}))  # noqa: S307 - only digits, operators, and/or/not
        except Exception:
            self.problems.append(f"{where}: cannot read the condition `{text.strip()}`")
            return False

    def read(self, rel, first=True):
        source = source_of(rel)
        if source is None or rel in self.seen:
            return
        self.seen.add(rel)
        mine = self.visible.setdefault(rel, [])
        # A backslash at the end of a line continues it (a long #define).
        lines, pending, start = [], "", 0
        for number, line in enumerate(source.code_str.split("\n"), 1):
            if not pending:
                start = number
            if line.rstrip().endswith("\\"):
                pending += line.rstrip()[:-1] + " "
                continue
            lines.append((start, pending + line))
            pending = ""
        stack = []             # per open #if: [parent is in, a branch was taken, this branch is in]
        for number, line in lines:
            m = DIRECTIVE.match(line)
            on = stack[-1][2] if stack else True
            if not m:
                if on and line.strip():
                    mine.append(line.strip())
                continue
            word, arg = m.group(1), m.group(2).strip()
            where = f"src/{rel}:{number}" if first else None
            if word in ("if", "ifdef", "ifndef"):
                if not on:
                    stack.append([False, True, False])
                    continue
                if word == "if":
                    value = self.condition(arg, where) if first else self.quiet(arg)
                else:
                    value = (arg in self.macros) == (word == "ifdef")
                stack.append([True, value, value])
            elif word == "elif" and stack:
                frame = stack[-1]
                if not frame[0] or frame[1]:
                    frame[2] = False
                else:
                    value = self.condition(arg, where) if first else self.quiet(arg)
                    frame[1], frame[2] = value, value
            elif word == "else" and stack:
                frame = stack[-1]
                frame[2] = frame[0] and not frame[1]
                frame[1] = True
            elif word == "endif" and stack:
                stack.pop()
            elif not on:
                continue
            elif word == "define":
                d = re.match(r"(\w+)(\([^)]*\))?\s*(.*)$", arg)
                if d:
                    self.macros[d.group(1)] = d.group(3).strip()
            elif word == "undef":
                self.macros.pop(arg, None)
            elif word == "include":
                q = re.match(r'"([^"]+)"', arg)
                if q:
                    for candidate in (q.group(1), (Path(rel).parent / q.group(1)).as_posix()):
                        if (src / candidate).is_file():
                            self.read(candidate, first=False)
                            break

    def quiet(self, text):
        """A condition of an included file: read if it can be, false if not,
        and no report."""
        before = len(self.problems)
        value = self.condition(text, "")
        del self.problems[before:]
        return value


# What each build environment sets. The documentation environment sets
# SSC_DOC_BUILD alone: app_config.h makes SSC_ACCEPTANCE of it.
CONFIGS = {
    "release": (),
    "acceptance": ("SSC_ACCEPTANCE",),
    "doc": ("SSC_DOC_BUILD",),
}
_units = {}


def unit(config, rel):
    """src/<rel> read under the configuration (cached)."""
    if (config, rel) not in _units:
        u = Unit(CONFIGS[config])
        u.read(rel)
        _units[(config, rel)] = u
        for problem in u.problems:
            errors.append(f"{problem} (read as {config})")
    return _units[(config, rel)]


def visible(config, rel):
    """The code of src/<rel> that is in under the configuration, blanks collapsed."""
    return collapse(" ".join(unit(config, rel).visible.get(rel, [])))


def opener(line):
    """The condition of an #if / #ifdef / #ifndef line without blanks, as
    `defined(NAME)` or `!defined(NAME)` for the common spellings (#ifdef NAME,
    #if defined NAME); None for any other line."""
    m = re.match(r"#\s*(if|ifdef|ifndef)\b\s*(.*)$", line)
    if not m:
        return None
    word, arg = m.group(1), m.group(2).strip()
    if word == "ifdef":
        return f"defined({arg})"
    if word == "ifndef":
        return f"!defined({arg})"
    return re.sub(r"\s+", "", re.sub(r"\bdefined\s+(\w+)", r"defined(\1)", arg))


def top_block(rel, condition):
    """(opener index, #else index or None, #endif index) in code_lines(rel) of
    the first block that opens with the condition, or None."""
    lines = code_lines(rel)
    for at, (_, line) in enumerate(lines):
        if opener(line) != condition:
            continue
        depth, other = 0, None
        for k in range(at, len(lines)):
            word = DIRECTIVE.match(lines[k][1])
            word = word.group(1) if word else ""
            if word in ("if", "ifdef", "ifndef"):
                depth += 1
            elif word in ("else", "elif") and depth == 1 and other is None:
                other = k
            elif word == "endif":
                depth -= 1
                if depth == 0:
                    return at, other, k
        return None
    return None


# ------------------------------------------------------------------ 1. app_config.h
# The version suffix and SSC_ACCEPTANCE come out of app_config.h: a release
# has neither switch and no suffix, the acceptance firmware the suffix
# "A" (Spec V61), the documentation firmware "-doku" and, from its one switch, the
# other one as well. FW_VERSION is the release literal and the suffix, and the
# only macro there whose name begins with FW_VERSION: the release run and
# scripts/check.sh read the literal from that line.
CONFIG_H = "app_config.h"
release_cfg, accept_cfg, doc_cfg = (unit(name, CONFIG_H).macros for name in ("release", "acceptance", "doc"))
expect("app_config.h: a release must not define SSC_ACCEPTANCE or SSC_DOC_BUILD",
       "SSC_ACCEPTANCE" not in release_cfg and "SSC_DOC_BUILD" not in release_cfg)
expect('app_config.h: a release must have an empty SSC_VERSION_SUFFIX ("")', release_cfg.get("SSC_VERSION_SUFFIX") == '""',
       f"SSC_VERSION_SUFFIX is {release_cfg.get('SSC_VERSION_SUFFIX')}")
expect('app_config.h: -DSSC_ACCEPTANCE must give SSC_VERSION_SUFFIX "A" (Spec V61) and no SSC_DOC_BUILD',
       accept_cfg.get("SSC_VERSION_SUFFIX") == '"A"' and "SSC_DOC_BUILD" not in accept_cfg,
       f"SSC_VERSION_SUFFIX is {accept_cfg.get('SSC_VERSION_SUFFIX')}")
expect('app_config.h: -DSSC_DOC_BUILD alone must give SSC_ACCEPTANCE and SSC_VERSION_SUFFIX "-doku"',
       "SSC_ACCEPTANCE" in doc_cfg and doc_cfg.get("SSC_VERSION_SUFFIX") == '"-doku"',
       f"SSC_ACCEPTANCE defined: {'SSC_ACCEPTANCE' in doc_cfg}, SSC_VERSION_SUFFIX is {doc_cfg.get('SSC_VERSION_SUFFIX')}")
expect('app_config.h: FW_VERSION must be the release literal followed by SSC_VERSION_SUFFIX',
       re.fullmatch(r'"v\d+\.\d+\.\d+(-beta\.\d+)?"\s+SSC_VERSION_SUFFIX', release_cfg.get("FW_VERSION", "")) is not None,
       f"FW_VERSION is {release_cfg.get('FW_VERSION')}")
config_source = source_of(CONFIG_H)
if config_source is not None:
    names = re.findall(r"^\s*#\s*define\s+(FW_VERSION\w*)", config_source.code, re.MULTILINE)
    expect("app_config.h: FW_VERSION must be the only macro whose name begins with FW_VERSION", names == ["FW_VERSION"],
           f"found {names}")

# No second place defines a switch: they come from the build environment, and
# SSC_ACCEPTANCE also from app_config.h, derived from SSC_DOC_BUILD.
for path in sorted(src.rglob("*")):
    if not path.is_file() or path.suffix not in (".h", ".hpp", ".c", ".cpp", ".ino"):
        continue
    rel = path.relative_to(src).as_posix()
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        m = re.match(r"\s*#\s*define\s+(SSC_ACCEPTANCE|SSC_DOC_BUILD)\b", line)
        if m and not (rel == CONFIG_H and m.group(1) == "SSC_ACCEPTANCE"):
            errors.append(f"src/{rel}:{number}: #define {m.group(1)}: a switch is set by its build environment only")

# ------------------------------------------------------------------ 2. the pure rules
# The rules of the two builds (touch, simulated weight and when it runs out,
# the write lock) have no switch around them: the host test compiles them
# without one and must always see them.
for rel in ("services/doc_build_rules.h", "services/doc_build_rules.cpp"):
    source = source_of(rel)
    if source is not None:
        expect(f"src/{rel}: the rules must not depend on a switch (the host test compiles them without one)",
               re.search(r"\bSSC_(ACCEPTANCE|DOC_BUILD)\b", source.code_str) is None)

# ------------------------------------------------------------------ 3. the files of the build
# doc_build.cpp (the state) and web_doc.cpp (the routes) are there for the
# acceptance firmware and the documentation firmware, and compile to nothing in
# a release: after their includes they begin with #if defined(SSC_ACCEPTANCE)
# and that block is the rest of the file. Their own header comes before the
# #if, and that header includes app_config.h, so that a build with
# -DSSC_DOC_BUILD alone has SSC_ACCEPTANCE by then: without the include the
# documentation firmware would silently lack the remote control.
STATE_API = {       # name: (return type, what the stub does)
    "docTouchStart": ("void", "nothing"),
    "docTouchPoint": ("bool", "false"),
    "docTouchCancel": ("void", "nothing"),
    "docSimSet": ("void", "nothing"),
    "docSimOff": ("void", "nothing"),
    "docSimActive": ("bool", "false"),
    "docSimGrams": ("float", "anything"),
    "docSimTick": ("void", "nothing"),
}
ROUTES_API = {"docRoutes": ("void", "nothing")}
SWITCH_ON = "defined(SSC_ACCEPTANCE)"

for cpp, header in (("app/doc_build.cpp", "app/doc_build.h"), ("web/web_doc.cpp", "web/web_doc.h")):
    lines = code_lines(cpp)
    if not lines:
        continue
    includes = []
    while len(includes) < len(lines) and re.match(r'#\s*include\b', lines[len(includes)][1]):
        includes.append(lines[len(includes)][1])
    expect(f'src/{cpp}: must include its own header "{header}" before the #if', any(
        re.fullmatch(r'#\s*include\s+"' + re.escape(header) + r'"', line) for line in includes))
    first = lines[len(includes)][1] if len(includes) < len(lines) else ""
    expect(f"src/{cpp}: after its includes it must begin with #if defined(SSC_ACCEPTANCE)", opener(first) == SWITCH_ON,
           f"the line after the includes is `{first}`")
    block = top_block(cpp, SWITCH_ON)
    if block is not None:
        expect(f"src/{cpp}: the #if defined(SSC_ACCEPTANCE) block must run to the end of the file",
               block[2] == len(lines) - 1 and block[1] is None,
               f"#endif at code line {block[2] + 1} of {len(lines)}, #else/#elif: {block[1] is not None}")
    # What a release makes of it: nothing. What the other two make of it: code.
    expect(f"src/{cpp}: a release (no switch) must compile it to nothing", visible("release", cpp) == "",
           visible("release", cpp)[:120])
    expect(f"src/{cpp}: the acceptance firmware (-DSSC_ACCEPTANCE) must have its code", visible("acceptance", cpp) != "")
    expect(f"src/{cpp}: the documentation firmware (-DSSC_DOC_BUILD alone) must have its code: "
           f"the header includes app_config.h, which sets SSC_ACCEPTANCE", visible("doc", cpp) != "")

for rel, api in (("app/doc_build.h", STATE_API), ("web/web_doc.h", ROUTES_API)):
    lines = code_lines(rel)
    if not lines:
        continue
    config_at = next((at for at, (_, line) in enumerate(lines)
                      if re.fullmatch(r'#\s*include\s+"app_config\.h"', line)), None)
    block = top_block(rel, SWITCH_ON)
    expect(f'src/{rel}: must include "app_config.h" (it makes SSC_ACCEPTANCE of SSC_DOC_BUILD)', config_at is not None)
    expect(f"src/{rel}: must have an #if defined(SSC_ACCEPTANCE) block", block is not None)
    if block is not None and config_at is not None:
        expect(f'src/{rel}: must include "app_config.h" before the #if defined(SSC_ACCEPTANCE)', config_at < block[0])
    if block is not None:
        expect(f"src/{rel}: the #if defined(SSC_ACCEPTANCE) block must have an #else branch with the stubs",
               block[1] is not None and lines[block[1]][1].startswith("#else"))
    for config, text in ((c, visible(c, rel)) for c in CONFIGS):
        stubbed = config == "release"
        for name, (kind, does) in api.items():
            stub = re.search(r"\bstatic\s+inline\b[^;{}]*?\b" + name + r"\s*\([^)]*\)\s*\{([^{}]*)\}", text)
            declared = re.search(r"\b(?:void|bool|float)\s+" + name + r"\s*\([^)]*\)\s*;", text)
            if stubbed:
                expect(f"src/{rel}: a release must have {name}() as a static inline stub", stub is not None)
                expect(f"src/{rel}: a release must not declare the real {name}()", declared is None)
                if stub is not None:
                    body = collapse(stub.group(1))
                    if does == "false":
                        expect(f"src/{rel}: the stub of {name}() must return false", re.fullmatch(r"return false;", body) is not None,
                               f"body `{body}`")
                    elif does == "nothing":
                        expect(f"src/{rel}: the stub of {name}() must do nothing", body == "", f"body `{body}`")
            else:
                expect(f"src/{rel}: the {config} firmware must declare {name}()", declared is not None)
                expect(f"src/{rel}: the {config} firmware must not have the stub of {name}()", stub is None)

# ------------------------------------------------------------------ 4. the routes are spelled in one place
# The text search of the release run looks for these two texts in every file it
# attaches. Everywhere in src/ but web_doc.cpp they are absent, comments
# included, so that the search needs no exception and a hit means a switch got
# into a release.
ROUTE_TEXTS = ("/api/touch", "/api/sim/weight")
ROUTE_FILE = "web/web_doc.cpp"
found_in_route_file = set()
for path in sorted(src.rglob("*")):
    if not path.is_file():
        continue
    rel = path.relative_to(src).as_posix()
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        for text in ROUTE_TEXTS:
            if text in line:
                if rel == ROUTE_FILE:
                    found_in_route_file.add(text)
                else:
                    errors.append(f"src/{rel}:{number}: `{text}` may be written in src/{ROUTE_FILE} only: {line.strip()[:100]}")
for text in ROUTE_TEXTS:
    expect(f"src/{ROUTE_FILE}: must spell the route {text} (the acceptance firmware answers it)", text in found_in_route_file)

# ------------------------------------------------------------------ 5. where the build is called from
# The callers have no #if of their own: the stubs of the two headers carry a
# release. And what the callers do is what the acceptance needs: a web touch
# replaces the finger in the touch read, a simulated weight replaces the
# reading of the load cell, the routes are registered with the logs page's,
# and the main loop lets a simulated weight run out.
CALLERS = ("hardware/display.cpp", "hardware/scale.cpp", "web/pages/page_logs.cpp", "app/app_loop.cpp")
for rel in CALLERS:
    source = source_of(rel)
    if source is not None:
        expect(f"src/{rel}: must not mention a switch (the stubs in the headers carry a release)",
               re.search(r"\bSSC_(ACCEPTANCE|DOC_BUILD)\b", source.code_str) is None)
for rel, name, calls in (
        ("hardware/display.cpp", "lvgl_touch", ("docTouchPoint", "docTouchCancel")),
        ("hardware/scale.cpp", "scaleHardwareAvailable", ("docSimActive",)),
        ("hardware/scale.cpp", "scaleHardwareReadRaw", ("docSimActive", "docSimRaw")),
        ("web/pages/page_logs.cpp", "routes", ("docRoutes",))):
    body = body_of(rel, name)
    for call in calls:
        if body is not None:
            expect(f"src/{rel}: {name}() must call {call}()", re.search(r"\b" + call + r"\s*\(", body) is not None)
loop = source_of("app/app_loop.cpp")
if loop is not None:
    expect("src/app/app_loop.cpp: must call docSimTick() (a simulated weight runs out after 15 minutes)",
           re.search(r"\bdocSimTick\s*\(", loop.code) is not None)

# ------------------------------------------------------------------ 6. what the build keeps
# A simulated weight lives in RAM only: a restart ends it, as the 15 minutes
# do, so the scale is never left with a weight that nobody put on it. The
# build's log lines say "Acceptance build:" (it serves both builds).
for rel in ("app/doc_build.cpp", "web/web_doc.cpp"):
    source = source_of(rel)
    if source is None:
        continue
    expect(f"src/{rel}: must keep nothing in flash (no preferences, NVS, file or SD write): a simulated weight ends with the restart",
           re.search(r"\b(prefsPut\w*|Preferences|nvs_\w+|SD\.open|LittleFS|SPIFFS)\b", source.code) is None)
    literals = [source.text[a:b] for a, b in source.strings]
    expect(f'src/{rel}: its log lines must begin with "Acceptance build:"', any(lit.startswith('"Acceptance build:') for lit in literals))
    expect(f'src/{rel}: no log line may begin with "Doc build:" (the build is the acceptance build, too)',
           not any(lit.startswith('"Doc build:') for lit in literals))
state = source_of("app/doc_build.cpp")
if state is not None:
    body = body_of("app/doc_build.cpp", "docSimTick")
    if body is not None:
        expect("src/app/doc_build.cpp: docSimTick() must test docSimExpired() against the time it was given",
               re.search(r"\bdocSimExpired\s*\([^)]*\bnow_ms\b", body) is not None)
    expect('src/app/doc_build.cpp: must log "Acceptance build: simulated weight off after 15 min" when it runs out',
           any(lit == '"Acceptance build: simulated weight off after 15 min"' for lit in (state.text[a:b] for a, b in state.strings)))
    body = body_of("app/doc_build.cpp", "docSimSet")
    if body is not None:
        expect("src/app/doc_build.cpp: docSimSet() must note the time (millis()) the weight was set",
               re.search(r"\bmillis\s*\(\s*\)", body) is not None)
for rel in ("services/doc_build_rules.h",):
    source = source_of(rel)
    if source is not None:
        expect(f"src/{rel}: must declare bool docSimExpired(uint32_t set_ms, uint32_t now_ms) and DOC_SIM_MAX_MS 900000",
               re.search(r"\bbool\s+docSimExpired\s*\(\s*uint32_t\s+\w+\s*,\s*uint32_t\s+\w+\s*\)\s*;", source.code) is not None
               and re.search(r"#\s*define\s+DOC_SIM_MAX_MS\s+900000\b", source.code) is not None)

# ------------------------------------------------------------------ 7. what the documentation build adds
# Only the documentation firmware (SSC_DOC_BUILD) writes nothing to any server,
# printer or tag and hides the red update dot: the screens that go into the
# documentation must neither change anything outside the scale nor show a dot. The acceptance firmware
# (SSC_ACCEPTANCE alone) is the opposite case on purpose: an acceptance sends
# weights, creates and deletes test spools, assigns slots, so a write lock
# that reached it would fail every send of the acceptance. Hence each of
# these is read for all three configurations.
#   - the writing methods of BackendHttp (sendRequest, POST, PATCH, PUT) are
#     overridden to ask docRequestAllowed() first, in the header and in the
#     implementation, with the path of the request (HTTPClient's _uri): the
#     one POST the rule lets through, Moonraker's query of the printer
#     objects, is told apart by its path
#   - showUpdateBadges() sets show = false
#   - every version text on the display comes from fwVersionShown(), which is
#     FW_VERSION in a release and in the acceptance firmware (it says -abnahme
#     there) and the release version in the documentation firmware: the header
#     line of the main screen, the info screen and the three firmware screens
#     (the browser upload's row, the GitHub update's status, the OTA menu). The
#     web pages, the log, status.json, mDNS and the version comparison keep
#     FW_VERSION: they are about the build that runs, not about a screenshot.
WRITE_METHODS = ("sendRequest", "POST", "PATCH", "PUT")
for rel, pattern in (("services/backend_http.h", r"\bint\s+{name}\s*\("),
                     ("services/backend_http.cpp", r"\bint\s+BackendHttp::{name}\s*\(")):
    for config in CONFIGS:
        text = visible(config, rel)
        for name in WRITE_METHODS:
            found = re.search(pattern.format(name=name), text) is not None
            if config == "doc":
                expect(f"src/{rel}: the documentation firmware (-DSSC_DOC_BUILD) must override {name}() to refuse a write", found)
            else:
                expect(f"src/{rel}: the {config} build must not override {name}() (the write lock belongs to -DSSC_DOC_BUILD only)",
                       not found)
backend = "services/backend_http.cpp"
for config in CONFIGS:
    text = visible(config, backend)
    asks = re.search(r"\bdocRequestAllowed\s*\(", text) is not None
    if config == "doc":
        expect(f"src/{backend}: the documentation firmware must ask docRequestAllowed() before it sends", asks)
        expect(f"src/{backend}: the documentation firmware must hand docRequestAllowed() the path of the request "
               "(_uri.c_str()), or Moonraker's object query cannot be told from a write",
               re.search(r"\bdocRequestAllowed\s*\(\s*\w+\s*,\s*_uri\.c_str\s*\(\s*\)\s*\)", text) is not None)
    else:
        expect(f"src/{backend}: the {config} build must not ask docRequestAllowed() (it would refuse a send)", not asks)

# Nor to a printer or a tag (Spec 5.3 AA: every way out; Punkteliste n54).
# Each has one door, and the documentation firmware has no call through it:
#   - a label, over WLAN or Bluetooth, leaves only by labelPrinterPrint() in
#     services/label_printer.cpp, the one caller of the three drivers; the
#     documentation firmware compiles none of those calls and answers with
#     docLabelNotPrinted() instead: nothing is sent, the screen shows what a
#     printer's answer shows, so a run of labels goes on for the camera
#   - a tag is written only in hardware/nfc.cpp, the one caller of the
#     reader's two writing commands; the documentation firmware compiles
#     neither and refuses with docTagWriteRefused(), like a write the tag did
#     not take, and the job ends with its usual error
# The acceptance firmware keeps every call: it prints and writes like a
# release, and a lock there would leave both untested.
DOORS = (
    ("services/label_printer.cpp", ("brotherQlPrint", "niimbotPrint", "phomemoMSeriesPrint"), "docLabelNotPrinted"),
    ("hardware/nfc.cpp", ("ntag2xx_WritePage", "mifareclassic_WriteDataBlock"), "docTagWriteRefused"),
)
for rel, calls, stand_in in DOORS:
    for config in CONFIGS:
        text = visible(config, rel)
        has_stand_in = re.search(r"\b" + stand_in + r"\s*\(", text) is not None
        for call in calls:
            found = re.search(r"\b" + call + r"\s*\(", text) is not None
            if config == "doc":
                expect(f"src/{rel}: the documentation firmware (-DSSC_DOC_BUILD) must not compile the call of {call}()", not found)
            else:
                expect(f"src/{rel}: the {config} build must keep the call of {call}()", found)
        if config == "doc":
            expect(f"src/{rel}: the documentation firmware must answer with {stand_in}()", has_stand_in)
        else:
            expect(f"src/{rel}: the {config} build must not have {stand_in}() (it prints and writes tags)", not has_stand_in)
# Where each of these may be named at all: the door, the driver that defines
# it and its header, and the driver that is its only caller further down. A
# call from anywhere else would be a way out past the lock.
NAMED_ONLY_IN = {
    "brotherQlPrint": ("services/label_printer.cpp", "services/brother_ql.cpp", "services/brother_ql.h"),
    "niimbotPrint": ("services/label_printer.cpp", "services/niimbot.cpp", "services/niimbot.h"),
    "phomemoMSeriesPrint": ("services/label_printer.cpp", "services/phomemo_m_series.cpp", "services/phomemo_m_series.h"),
    "bleWriteBlocks": ("services/phomemo_m_series.cpp", "services/ble_service.cpp", "services/ble_service.h"),
    "bleTalkOpen": ("services/niimbot.cpp", "services/ble_service.cpp", "services/ble_service.h"),
    "bleTalkSend": ("services/niimbot.cpp", "services/ble_service.cpp", "services/ble_service.h"),
    "ntag2xx_WritePage": ("hardware/nfc.cpp",),
    "mifareclassic_WriteDataBlock": ("hardware/nfc.cpp",),
}
for path in sorted(src.rglob("*.cpp")) + sorted(src.rglob("*.h")):
    rel = path.relative_to(src).as_posix()
    code = source_of(rel).code
    for name, allowed in NAMED_ONLY_IN.items():
        if rel not in allowed and re.search(r"\b" + name + r"\s*\(", code):
            expect(f"src/{rel}: calls {name}(), which only {allowed[0]} may (the documentation firmware locks it there)", False)
    # The Brother's raw line is the one socket the firmware opens to a device
    # by hand; another would be a way out that no lock sees.
    if rel != "services/brother_ql.cpp" and re.search(r"\.\s*connect\s*\(", code):
        expect(f"src/{rel}: opens a connection of its own (.connect()), past the documentation firmware's locks", False)

badges = "ui/update_badges.cpp"
for config in CONFIGS:
    hides = re.search(r"\bshow\s*=\s*false\s*;", visible(config, badges)) is not None
    if config == "doc":
        expect(f"src/{badges}: the documentation firmware must set show = false in showUpdateBadges() (no red dot on a screenshot)", hides)
    else:
        expect(f"src/{badges}: the {config} build must not set show = false (the dot is what the update check shows)", not hides)
# The other way a dot gets onto a screen: createUpdateBadge() shows the new badge
# at once when an update was found before its screen was built, and the settings,
# system and OTA screens are built when they are opened. The documentation
# firmware, which is offered the release it was built on, must keep that dot off
# too; the other builds show it as the update check says.
for config in CONFIGS:
    text = visible(config, badges)
    as_found = re.search(r"\bsetBadgeVisible\s*\(\s*badge\s*,\s*update_available\s*\)", text) is not None
    hidden = re.search(r"\bsetBadgeVisible\s*\(\s*badge\s*,\s*false\s*\)", text) is not None
    if config == "doc":
        expect(f"src/{badges}: the documentation firmware must make a new badge hidden (a screen built after the update check would carry the dot)",
               hidden and not as_found)
    else:
        expect(f"src/{badges}: the {config} build must make a new badge visible as update_available says", as_found and not hidden)

SHOWN = r"\bconst\s+char\s*\*\s*fwVersionShown\s*\(\s*(?:void)?\s*\)"
for config in CONFIGS:
    text = visible(config, "app/doc_build.h")
    expect(f"src/app/doc_build.h: the {config} build must have const char* fwVersionShown()", re.search(SHOWN, text) is not None)
release_header = visible("release", "app/doc_build.h")
expect("src/app/doc_build.h: in a release fwVersionShown() must be defined there and return FW_VERSION",
       re.search(SHOWN + r"\s*\{[^{}]*\bFW_VERSION\b", release_header) is not None)
for rel, name in (("ui/main_screen.cpp", "buildUI"), ("ui/info_screen.cpp", "infoVersionLine"),
                  ("ui/ota_browser.cpp", "webBuildFirmwareRow"), ("ui/ota_github.cpp", "ghBuildStatus"),
                  ("ui/ota_menu.cpp", "buildOtaScreen")):
    source = source_of(rel)
    if source is None:
        continue
    # parseVersion(FW_VERSION) is no text on the screen: the comparison must see
    # the suffix (version_compare.cpp ranks it), so that one stays.
    on_screen = re.sub(r"\bparseVersion\s*\(\s*FW_VERSION\s*\)", "", source.code)
    expect(f"src/{rel}: must show the version with fwVersionShown(), not FW_VERSION",
           re.search(r"\bFW_VERSION\b", on_screen) is None)
    body = body_of(rel, name)
    if body is not None:
        expect(f"src/{rel}: {name}() must call fwVersionShown()", re.search(r"\bfwVersionShown\s*\(\s*\)", body) is not None)
    expect(f"src/{rel}: must not mention a switch (fwVersionShown() carries it)",
           re.search(r"\bSSC_(ACCEPTANCE|DOC_BUILD)\b", source.code_str) is None)

# ------------------------------------------------------------------ 8. the build environments
# The switches come from two environments of their own, extending the 7B's, and
# from nowhere else: not from [common] or [env] (every board would carry the
# remote control), not from a board's environment (a release builds those),
# not from default_envs (a plain `pio run` would build them). Each of the two
# sets exactly its own switch; the documentation environment sets
# SSC_DOC_BUILD alone, app_config.h derives the other.
INI = root / "platformio.ini"
ABNAHME_ENV, DOKU_ENV = "waveshare-s3-lcd7b-abnahme", "waveshare-s3-lcd7b-doku"
BASE_ENV = "waveshare-s3-lcd7b"


def read_ini(path):
    """{section: {key: value}} of a PlatformIO ini: full-line comments dropped,
    an indented line continues the value above it (joined with newlines)."""
    sections, current, key = {}, None, None
    for raw in path.read_text(encoding="utf-8").splitlines():
        stripped = raw.strip()
        if not stripped or stripped[0] in ";#":
            continue
        section = re.match(r"^\[([^\]]+)\]\s*$", raw)
        if section:
            current, key = sections.setdefault(section.group(1), {}), None
        elif raw[0] in " \t" and current is not None and key is not None:
            current[key] += "\n" + stripped
        else:
            pair = re.match(r"^([\w.\-]+)\s*[=:]\s*(.*)$", raw)
            if pair and current is not None:
                key = pair.group(1)
                current[key] = pair.group(2).strip()
    return sections


ini = read_ini(INI) if INI.is_file() else {}
expect("platformio.ini: must be there", INI.is_file())
own_sections = {f"env:{ABNAHME_ENV}", f"env:{DOKU_ENV}"}
for section, keys in ini.items():
    if section in own_sections:
        continue
    switches = sorted(set(re.findall(r"SSC_(?:ACCEPTANCE|DOC_BUILD)", " ".join(keys.values()))))
    expect(f"platformio.ini [{section}]: must set no switch (a release builds it, or all boards inherit it)", not switches,
           f"found {switches}")
expect("platformio.ini [platformio]: default_envs must not name the acceptance or documentation environment",
       not re.search(r"abnahme|doku", ini.get("platformio", {}).get("default_envs", "")))
for env_name, flag, other in ((ABNAHME_ENV, "-DSSC_ACCEPTANCE", "SSC_DOC_BUILD"), (DOKU_ENV, "-DSSC_DOC_BUILD", "SSC_ACCEPTANCE")):
    section = ini.get(f"env:{env_name}")
    expect(f"platformio.ini: must have [env:{env_name}]", section is not None)
    if section is None:
        continue
    expect(f"platformio.ini [env:{env_name}]: must extend env:{BASE_ENV}", section.get("extends") == f"env:{BASE_ENV}",
           f"extends = {section.get('extends')}")
    flags = section.get("build_flags", "")
    expect(f"platformio.ini [env:{env_name}]: build_flags must keep the 7B's (${{env:{BASE_ENV}.build_flags}})",
           f"${{env:{BASE_ENV}.build_flags}}" in flags, f"build_flags = {flags!r}")
    defined = re.findall(r"-D\s*(SSC_\w+)", flags)
    expect(f"platformio.ini [env:{env_name}]: build_flags must set {flag} and no other SSC_ switch "
           f"(app_config.h derives the rest)", defined == [flag[2:]], f"sets {defined}")
    expect(f"platformio.ini [env:{env_name}]: must not mention {other}", other not in " ".join(section.values()))

# ------------------------------------------------------------------ 9. the workflows
# A release builds the three boards of src/boards by their own environments:
# neither workflow's build matrix, nor the list of the other boards of the
# release, may name the acceptance or the documentation environment, and the
# release workflow sets no switch by hand either (-D flags, or
# PLATFORMIO_BUILD_FLAGS in the environment of a step). The acceptance firmware
# is built by a job of its own in ci.yml, for a push to a work branch only:
# the 7B's size check and board line as for any board, then the proof that
# firmware.bin has the remote control in it (a build whose switch was lost
# would otherwise be flashed as the acceptance firmware and show nothing),
# then an artifact that is kept one day. The documentation firmware has a job
# of its own, on request only (workflow_dispatch), kept one day as well.
CI = root / ".github" / "workflows" / "ci.yml"
RELEASE = root / ".github" / "workflows" / "build-all-boards.yml"


def workflow_code(path):
    """The lines of a workflow without comment-only lines."""
    return [line for line in path.read_text(encoding="utf-8").splitlines() if line.strip() and not line.lstrip().startswith("#")]


def parse_job_steps(entries):
    """[step] of a job's lines [(indent, text)]: a step is a dict with name, if,
    uses, run (the text of a run block or a one-line run, None when there is
    none), with ({key: value}) and text (all its lines)."""
    start = next((at for at, (indent, line) in enumerate(entries) if indent == 4 and line == "steps:"), None)
    if start is None:
        return []
    items = []
    for indent, line in entries[start + 1:]:
        if indent <= 4:
            break
        if indent == 6 and line.startswith("- "):
            items.append([])
        if items:
            items[-1].append((indent, line))
    steps = []
    for item in items:
        dash = item[0][0]
        lines = [(dash + 2, item[0][1][2:])] + item[1:]
        step = {"name": None, "if": None, "uses": None, "run": None, "with": {}, "text": "\n".join(line for _, line in lines)}
        at = 0
        while at < len(lines):
            indent, line = lines[at]
            pair = re.match(r"^([\w-]+):\s*(.*)$", line) if indent == dash + 2 else None
            at += 1
            if not pair:
                continue
            key, value = pair.groups()
            children = []
            while at < len(lines) and lines[at][0] > dash + 2:
                children.append(lines[at])
                at += 1
            if key == "run":
                step["run"] = "\n".join(line for _, line in children) if value.startswith("|") else value
            elif key == "with":
                for _, child in children:
                    kv = re.match(r"^([\w-]+):\s*(.*)$", child)
                    if kv:
                        step["with"][kv.group(1)] = kv.group(2)
            elif key in ("name", "if", "uses"):
                step[key] = value
        steps.append(step)
    return steps


def read_jobs(path):
    """{job id: {"lines": [(indent, text)], "if": the job's if: or None, "matrix": bool, "steps": [step]}}"""
    jobs, current, in_jobs = {}, None, False
    for line in workflow_code(path):
        indent = len(line) - len(line.lstrip())
        if indent == 0:
            in_jobs = line.strip() == "jobs:"
            current = None
        elif in_jobs and indent == 2 and re.match(r"^\s{2}[\w-]+:\s*$", line):
            current = jobs.setdefault(line.strip()[:-1], {"lines": []})
        elif in_jobs and current is not None:
            current["lines"].append((indent, line.strip()))
    for job in jobs.values():
        job["if"] = next((line[3:].strip() for indent, line in job["lines"] if indent == 4 and line.startswith("if:")), None)
        job["matrix"] = any(line.startswith("matrix:") for _, line in job["lines"])
        job["steps"] = parse_job_steps(job["lines"])
    return jobs


def github_condition(expression, context):
    """The truth of a GitHub Actions `if:` expression for a context ({'github.event_name': 'push', ...}).
    Reads ==, !=, &&, ||, !, brackets, strings and startsWith, endsWith, contains and fromJSON;
    ValueError for anything else."""
    expression = re.sub(r"^\$\{\{\s*(.*?)\s*\}\}$", r"\1", expression.strip())
    token = re.compile(r"\s*(?:(?P<string>'(?:[^']|'')*')|(?P<op>&&|\|\||==|!=|!|\(|\)|,)|(?P<name>[A-Za-z_][\w.\-]*))")
    python, at = [], 0
    helpers = {
        "startsWith": lambda a, b: str(a).lower().startswith(str(b).lower()),
        "endsWith": lambda a, b: str(a).lower().endswith(str(b).lower()),
        "contains": lambda a, b: (str(b).lower() in [str(x).lower() for x in a]) if isinstance(a, list)
        else str(b).lower() in str(a).lower(),
        "fromJSON": json.loads,
    }
    while at < len(expression):
        m = token.match(expression, at)
        if not m or m.end() == at:
            raise ValueError(f"cannot read `{expression}` at `{expression[at:at + 12]}`")
        at = m.end()
        if m.group("string") is not None:
            python.append(repr(m.group("string")[1:-1].replace("''", "'")))
        elif m.group("op") is not None:
            python.append({"&&": " and ", "||": " or ", "!": " not "}.get(m.group("op"), m.group("op")))
        else:
            name = m.group("name")
            if name in helpers:
                python.append(f"_f['{name}']")
            elif name in context:
                python.append(repr(context[name]))
            elif name in ("true", "false", "null"):
                python.append({"true": "True", "false": "False", "null": "None"}[name])
            else:
                raise ValueError(f"cannot read `{expression}`: `{name}` is not a context this test knows")
    try:
        return bool(eval("".join(python), {"__builtins__": {}}, {"_f": helpers}))  # noqa: S307 - built from the tokens above
    except Exception as problem:
        raise ValueError(f"cannot read `{expression}`: {problem}")


def event(name, ref_name):
    """The contexts of a run: a push or a manual run on a branch, a pull request."""
    if name == "pull_request":
        return {"github.event_name": name, "github.ref_name": ref_name, "github.ref": f"refs/pull/{ref_name}",
                "github.ref_type": "branch", "github.head_ref": "feat/abnahme-firmware", "github.base_ref": "dev"}
    return {"github.event_name": name, "github.ref_name": ref_name, "github.ref": f"refs/heads/{ref_name}",
            "github.ref_type": "branch", "github.head_ref": "", "github.base_ref": ""}


def runs(job, name, ref_name):
    """Whether the job runs for the event; None (with one error per if:) when
    its if: cannot be read."""
    if job["if"] is None:
        return True
    try:
        return github_condition(job["if"], event(name, ref_name))
    except ValueError as problem:
        message = f"ci.yml: the if: of a job cannot be evaluated: {problem}"
        if message not in errors:
            errors.append(message)
        return None


def run_shell(script, workdir, env):
    """The script as a step of the runner runs it (`bash -e`) in workdir; the file
    lies beside the folder, not in it. (returncode, output), or (None, reason)."""
    bash = shutil.which("bash")
    if bash is None:
        return None, "bash is not installed"
    script_file = Path(workdir).parent / "step.sh"
    with open(script_file, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(script + "\n")
    try:
        done = subprocess.run([bash, "-e", script_file.as_posix()], cwd=workdir, env={**os.environ, **env},
                              capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    except (OSError, subprocess.SubprocessError) as problem:
        return None, str(problem)
    return done.returncode, (done.stdout + done.stderr).strip()


# 9a. Nothing of the two builds in what a release builds.
for path in (RELEASE, CI):
    if not path.is_file():
        errors.append(f".github/workflows/{path.name}: the file is missing")
        continue
    code = workflow_code(path)
    for line in code:
        for lists in re.findall(r"\benv:\s*\[([^\]]*)\]", line) + re.findall(r"\bOTHER_BOARDS:\s*(.*)$", line):
            named = [name.strip() for name in re.split(r"[,\s]+", lists) if name.strip()]
            expect(f"{path.name}: a build matrix or the list of the other boards must not name the acceptance or "
                   f"documentation environment", not any(re.search(r"abnahme|doku", name) for name in named),
                   f"names {named}")
    if path == RELEASE:
        for line in code:
            if re.search(r"-D\s*SSC_|PLATFORMIO_BUILD_(SRC_)?FLAGS|PLATFORMIO_BUILD_UNFLAGS", line):
                errors.append(f"{path.name}: the release must set no switch, found `{line.strip()[:80]}`")
            if re.search(r"\bpio\s+run\b.*(abnahme|doku)", line):
                errors.append(f"{path.name}: the release must not build the acceptance or documentation environment, found `{line.strip()[:80]}`")

# 9b. The acceptance job and the documentation job in ci.yml.
jobs = read_jobs(CI) if CI.is_file() else {}
ci_text = CI.read_text(encoding="utf-8") if CI.is_file() else ""
on_block = re.search(r"^on:\s*\n((?:[ \t]+.*\n|\s*\n)+)", ci_text, re.MULTILINE)
expect("ci.yml: must be runnable by hand (workflow_dispatch: under on:) for the documentation firmware",
       on_block is not None and re.search(r"^\s{2}workflow_dispatch:", on_block.group(1), re.MULTILINE) is not None)
for env_name, label in ((ABNAHME_ENV, "acceptance"), (DOKU_ENV, "documentation")):
    named = [job_id for job_id, job in jobs.items()
             if any(env_name in (step["run"] or "") for step in job["steps"])]
    expect(f"ci.yml: exactly one job must build {env_name} (`pio run -e {env_name}`)", len(named) == 1, f"{len(named)} job(s): {named}")
    if len(named) != 1:
        continue
    job_id = named[0]
    job = jobs[job_id]
    expect(f"ci.yml job {job_id}: must not be a matrix job (the three release boards stay as they are)", not job["matrix"])
    expect(f"ci.yml job {job_id}: must not be the job that builds the release boards", job_id not in ("build", "checks"))
    if label == "acceptance":
        # A push to a work branch builds it; dev, main, a pull request and a manual run do not.
        wanted = [("push", "feat/abnahme-firmware"), ("push", "fix/lcd7b"), ("push", "feature/x")]
        unwanted = [("push", "dev"), ("push", "main"), ("pull_request", "12/merge"), ("workflow_dispatch", "feat/abnahme-firmware")]
    else:
        # On request only.
        wanted = [("workflow_dispatch", "feat/abnahme-firmware"), ("workflow_dispatch", "dev")]
        unwanted = [("push", "feat/abnahme-firmware"), ("push", "dev"), ("push", "main"), ("pull_request", "12/merge")]
    for name, ref in wanted:
        if runs(job, name, ref) is False:
            errors.append(f"ci.yml job {job_id}: must run for a {name} on {ref}, its if: is `{job['if']}`")
    for name, ref in unwanted:
        if runs(job, name, ref) is True:
            errors.append(f"ci.yml job {job_id}: must not run for a {name} on {ref}, its if: is `{job['if']}`")
    steps = job["steps"]
    uploads = [at for at, step in enumerate(steps) if (step["uses"] or "").startswith("actions/upload-artifact@")]
    expect(f"ci.yml job {job_id}: must upload exactly one artifact", len(uploads) == 1, f"{len(uploads)} upload(s)")
    if len(uploads) != 1:
        continue
    upload = steps[uploads[0]]
    expect(f"ci.yml job {job_id}: the artifact must be kept one day (retention-days: 1)",
           upload["with"].get("retention-days") == "1", f"retention-days: {upload['with'].get('retention-days')}")
    expect(f"ci.yml job {job_id}: must upload .pio/build/{env_name}/firmware.bin",
           upload["with"].get("path") == f".pio/build/{env_name}/firmware.bin", f"path: {upload['with'].get('path')}")
    build_at = next((at for at, step in enumerate(steps) if f"pio run -e {env_name}" in (step["run"] or "")), None)
    expect(f"ci.yml job {job_id}: must build before it uploads", build_at is not None and build_at < uploads[0])
    if label == "documentation":
        continue
    expect(f"ci.yml job {job_id}: the artifact must be named acceptance-{BASE_ENV}-${{{{ github.sha }}}}",
           upload["with"].get("name") == f"acceptance-{BASE_ENV}-${{{{ github.sha }}}}", f"name: {upload['with'].get('name')}")
    # The steps of the Build job that matter for an image that goes onto a scale.
    whole = "\n".join(step["text"] for step in steps)
    for what, needle in (("PlatformIO pinned to 6.2.0", "pip install platformio==6.2.0"),
                         ("the pioarduino pin", "pioarduino==6.2.0"),
                         ("the 6 MB size limit", "6291456"),
                         ("the board line check of the 7B",
                          f"python3 scripts/board_marker.py .pio/build/{env_name}/firmware.bin {BASE_ENV}")):
        expect(f"ci.yml job {job_id}: must have {what} like the Build job (`{needle}`)", needle in whole)
    # The proof that the remote control is in the image: before the upload, and it works.
    proofs = [at for at, step in enumerate(steps) if "/api/touch" in (step["run"] or "")]
    expect(f"ci.yml job {job_id}: exactly one step must check that firmware.bin holds /api/touch", len(proofs) == 1,
           f"{len(proofs)} step(s)")
    if len(proofs) == 1:
        expect(f"ci.yml job {job_id}: the /api/touch check must come after the build and before the upload",
               build_at is not None and build_at < proofs[0] < uploads[0])
        script = steps[proofs[0]]["run"]
        script = re.sub(r"\$\{\{\s*github\.sha\s*\}\}", "0" * 40, script)
        firmware = f".pio/build/{env_name}/firmware.bin"
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp) / "work"
            (work / firmware).parent.mkdir(parents=True)
            body = b"\x00\x01\x02SpoolmanScale-board:" + BASE_ENV.encode() + bytes(range(256)) + b"\x00end"
            for what, content, passes in (("holds /api/touch", body + b"\x00POST /api/touch\x00" + body, True),
                                          ("holds no /api/touch (the switch was lost)", body, False),
                                          ("holds /api/sim/weight but no /api/touch", body + b"\x00/api/sim/weight\x00", False)):
                (work / firmware).write_bytes(content)
                code, output = run_shell(script, work, {})
                if code is None:
                    expect(f"ci.yml job {job_id}: the /api/touch check must be runnable", False, output)
                    break
                expect(f"ci.yml job {job_id}: the /api/touch check must {'pass' if passes else 'fail'} when firmware.bin {what}",
                       (code == 0) == passes, f"exit {code}, output: {output[-300:]}")

# 9c. The release boards' own build job in ci.yml names no switch and none of the two environments.
build_job = jobs.get("build")
if build_job is not None:
    joined = "\n".join(line for _, line in build_job["lines"])
    expect("ci.yml job build: the matrix job of the release boards must not set a switch or build the acceptance or documentation environment",
           not re.search(r"SSC_ACCEPTANCE|SSC_DOC_BUILD|abnahme|doku", joined))

# ------------------------------------------------------------------ Verdict
if errors:
    print("test_build_variants: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("test_build_variants: ok")
