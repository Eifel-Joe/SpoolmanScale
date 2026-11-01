"""Read the source tree and the build files as text, without a compiler, and check that the acceptance and documentation builds stay what they are meant to be: switches, off in every release. SSC_ACCEPTANCE (the acceptance firmware: touch the display and simulate a weight from the web) and SSC_DOC_BUILD (the documentation firmware: the same, plus it writes nothing to any server and shows the release version) are set by one build environment each (platformio.ini) and by nothing else; app_config.h derives SSC_ACCEPTANCE from SSC_DOC_BUILD and builds the version suffix.

What the switches switch is read the way the preprocessor reads it, as far as `defined(...)` goes: each file is read three times, for a release (no switch), for the acceptance firmware (-DSSC_ACCEPTANCE) and for the documentation firmware (-DSSC_DOC_BUILD alone), following the project's own #include "..." lines, and the code that is left is what is checked: the files of the two builds are empty in a release, their header stubs take over, the write lock of the documentation build is not there in the acceptance firmware (it would refuse every send of an acceptance), and a release carries no route of the acceptance build. The routes /api/touch and /api/sim/weight are spelled in src/web/web_doc.cpp and nowhere else in src/, comments included, so that the text search of the release run needs no exceptions.

The rules (services/doc_build_rules.h) have their own test with a compiler, test_doc_build_rules.py; the version ranking has test_version_compare.py; what the release run does with these texts has test_release_workflow.py."""
import re
import sys
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
# "-abnahme", the documentation firmware "-doku" and, from its one switch, the
# other one as well. FW_VERSION is the release literal and the suffix, and the
# only macro there whose name begins with FW_VERSION: the release run and
# scripts/check.sh read the literal from that line.
CONFIG_H = "app_config.h"
release_cfg, accept_cfg, doc_cfg = (unit(name, CONFIG_H).macros for name in ("release", "acceptance", "doc"))
expect("app_config.h: a release must not define SSC_ACCEPTANCE or SSC_DOC_BUILD",
       "SSC_ACCEPTANCE" not in release_cfg and "SSC_DOC_BUILD" not in release_cfg)
expect('app_config.h: a release must have an empty SSC_VERSION_SUFFIX ("")', release_cfg.get("SSC_VERSION_SUFFIX") == '""',
       f"SSC_VERSION_SUFFIX is {release_cfg.get('SSC_VERSION_SUFFIX')}")
expect('app_config.h: -DSSC_ACCEPTANCE must give SSC_VERSION_SUFFIX "-abnahme" and no SSC_DOC_BUILD',
       accept_cfg.get("SSC_VERSION_SUFFIX") == '"-abnahme"' and "SSC_DOC_BUILD" not in accept_cfg,
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

# ------------------------------------------------------------------ Verdict
if errors:
    print("test_build_variants: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("test_build_variants: ok")
