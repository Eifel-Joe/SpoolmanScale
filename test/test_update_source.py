"""Read the source tree without a compiler and check that the update path asks this fork and not the original: the repo and the pages address are each defined once in app_config.h, the release download, the version check and the flasher take them from there, no file under src/ still names the original's repo or its pages root, and the links that must stay with the original (the manual, the FilaMan plugin) are still there."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The one place that names the fork. Everything else derives from it.
FW_REPO_DEFINE = re.compile(r'^\s*#define\s+FW_REPO\s+"Eifel-Joe/SpoolmanScale"\s*(?://.*)?$', re.MULTILINE)
FW_PAGES_DEFINE = re.compile(r'^\s*#define\s+FW_PAGES_URL\s+"https://eifel-joe\.github\.io/SpoolmanScale/"\s*(?://.*)?$', re.MULTILINE)
FLASHER_DEFINE = re.compile(r'^\s*#define\s+FLASHER_URL\s+FW_PAGES_URL\s*(?://.*)?$', re.MULTILINE)
GH_REPO_DEFINE = re.compile(r'^\s*#define\s+GH_REPO\s+FW_REPO\s*(?://.*)?$', re.MULTILINE)
VERSION_URL_PART = 'FW_PAGES_URL "version.json"'

# The original's repo (github.com/Niko11111/SpoolmanScale, followed by a quote,
# a slash or the end of the line) and its pages root (with the slash after
# SpoolmanScale: the manual lives under SpoolmanScale-Docs and stays). The
# FilaMan plugin repo starts the same way but is another repository.
ORIGINAL = re.compile(
    r"github\.com/niko11111/spoolmanscale(?:[\"'/]|$)"
    r"|niko11111\.github\.io/spoolmanscale/",
    re.IGNORECASE,
)

# Files that spell one of the two defines out and so must include app_config.h
# themselves: a build without a compiler here cannot tell an include that
# arrives through another header from one that does not.
DEFINE_USERS = [
    "services/github_release.cpp",
    "services/update_check.cpp",
    "ui/info_screen.cpp",
    "web/web_shell.cpp",
]
INCLUDE_CONFIG = re.compile(r'^\s*#include\s+"app_config\.h"', re.MULTILINE)

# What belongs to the original and must not disappear with the update source.
KEPT = [
    ("app_config.h", 'DOCS_URL "https://niko11111.github.io/SpoolmanScale-Docs/"'),
    ("web/pages/page_backend_embed.cpp", "Niko11111/filaman-spoolmanscale-plugin"),
]

errors = []


def read(rel):
    path = src / rel
    if not path.is_file():
        errors.append(f"src/{rel}: file is missing")
        return None
    return path.read_text(encoding="utf-8", errors="replace")


# 1. app_config.h defines the repo and the pages address once, and the flasher
# address is the pages address.
config = read("app_config.h")
if config is not None:
    if not FW_REPO_DEFINE.search(config):
        errors.append('src/app_config.h: no #define FW_REPO "Eifel-Joe/SpoolmanScale"')
    if not FW_PAGES_DEFINE.search(config):
        errors.append('src/app_config.h: no #define FW_PAGES_URL "https://eifel-joe.github.io/SpoolmanScale/"')
    if not FLASHER_DEFINE.search(config):
        errors.append("src/app_config.h: no #define FLASHER_URL FW_PAGES_URL")

# 2. The release download and the version check take them from there.
release = read("services/github_release.cpp")
if release is not None and not GH_REPO_DEFINE.search(release):
    errors.append("src/services/github_release.cpp: no #define GH_REPO FW_REPO")
check = read("services/update_check.cpp")
if check is not None and VERSION_URL_PART not in check:
    errors.append(f"src/services/update_check.cpp: version URL is not {VERSION_URL_PART}")

# 3. The files that use the defines include app_config.h.
for rel in DEFINE_USERS:
    text = read(rel)
    if text is not None and not INCLUDE_CONFIG.search(text):
        errors.append(f'src/{rel}: does not include "app_config.h"')

# 4. No file under src/ still names the original's repo or pages root.
for path in sorted(src.rglob("*")):
    if not path.is_file():
        continue
    rel = path.relative_to(src).as_posix()
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        match = ORIGINAL.search(line)
        if match:
            errors.append(f"src/{rel}:{number}: still names the original: {match.group(0)}")

# 5. The links that stay with the original are still there.
for rel, needle in KEPT:
    text = read(rel)
    if text is not None and needle not in text:
        errors.append(f"src/{rel}: lost {needle}")

# 6. Verdict.
if errors:
    print("update source: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("update source: ok")
