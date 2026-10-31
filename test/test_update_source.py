"""Read the source tree without a compiler and check that the update path asks this fork and not the original: the repo and the pages address are each defined once in app_config.h, the release download, the version check and the flasher take them from there, no file under src/ still names the original's repo or its pages root, and the links that must stay with the original (the manual, the FilaMan plugin, the Ko-fi, Discord and MakerWorld codes) are still there. The checks by hand read the version file the daily check reads, through the same choice of release, nothing asks GitHub for its release list, and the checksum goes with every tag a check found."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer and the function finder of the convention ratchet: comments
# and string contents blanked, function bodies by brace matching. The script
# is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

# The one place that names the fork. Everything else derives from it.
FW_REPO_DEFINE = re.compile(r'^\s*#define\s+FW_REPO\s+"Eifel-Joe/SpoolmanScale"\s*(?://.*)?$', re.MULTILINE)
FW_PAGES_DEFINE = re.compile(r'^\s*#define\s+FW_PAGES_URL\s+"https://eifel-joe\.github\.io/SpoolmanScale/"\s*(?://.*)?$', re.MULTILINE)
FLASHER_DEFINE = re.compile(r'^\s*#define\s+FLASHER_URL\s+FW_PAGES_URL\s*(?://.*)?$', re.MULTILINE)
GH_REPO_DEFINE = re.compile(r'^\s*#define\s+GH_REPO\s+FW_REPO\s*(?://.*)?$', re.MULTILINE)
# The version file, next to the flasher on our pages: defined once, in the
# unit that also says which of its releases a board takes.
VERSION_FILE = "services/version_file.h"
VERSION_FILE_DEFINE = re.compile(r'^\s*#define\s+VERSION_FILE_URL\s+FW_PAGES_URL\s+"version\.json"\s*(?://.*)?$',
                                 re.MULTILINE)

# The original's repo however it is reached (github.com/, api.github.com/repos/,
# raw.githubusercontent.com/ or a bare Niko11111/SpoolmanScale) and its pages
# root, each as a whole name: the manual lives under SpoolmanScale-Docs and
# stays. The FilaMan plugin repo (Niko11111/filaman-spoolmanscale-plugin) is
# another repository.
ORIGINAL = re.compile(
    r"niko11111/spoolmanscale(?![\w-])"
    r"|niko11111\.github\.io/spoolmanscale(?![\w-])",
    re.IGNORECASE,
)

# Files that spell one of the two defines out and so must include app_config.h
# themselves: a build without a compiler here cannot tell an include that
# arrives through another header from one that does not.
DEFINE_USERS = [
    "services/github_release.cpp",
    "services/update_check.cpp",
    "services/version_file.h",
    "ui/info_screen.cpp",
    "web/web_shell.cpp",
]
INCLUDE_CONFIG = re.compile(r'^\s*#include\s+"app_config\.h"', re.MULTILINE)

# What belongs to the original and must not disappear with the update source:
# the manual, the FilaMan plugin, and the codes of the info screen and the
# test label but GitHub's (Ko-fi, Discord, MakerWorld, the manual).
KEPT = [
    ("app_config.h", 'DOCS_URL "https://niko11111.github.io/SpoolmanScale-Docs/"'),
    ("app_config.h", 'DONATION_URL "ko-fi.com/formfollowsfunction"'),
    ("web/pages/page_backend_embed.cpp", "Niko11111/filaman-spoolmanscale-plugin"),
    ("ui/info_screen.cpp", '"https://ko-fi.com/formfollowsfunction"'),
    ("ui/info_screen.cpp", '"https://discord.gg/K3KmKnrWhP"'),
    ("ui/info_screen.cpp", '"https://makerworld.com/de/@FormFollowsF/upload"'),
    ("ui/info_screen.cpp", '"https://niko11111.github.io/SpoolmanScale-Docs/"'),
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
version_file = read(VERSION_FILE)
if version_file is not None and not VERSION_FILE_DEFINE.search(version_file):
    errors.append(f'src/{VERSION_FILE}: no #define VERSION_FILE_URL FW_PAGES_URL "version.json"')

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

# 6. The checks by hand read the file the daily check reads (STATUS 33, 34).
# GitHub's release list, which "Check" on the scale and the web page used to
# read, carries no checksum: an install right after a check by hand, before
# the daily check had run, went in "(no checksum published)" though one was
# published, and the list, 55 kB as one String, came back cut at 32 KiB once.
# Now the daily check and the check by hand (githubLatestTag(), which both
# "Check" buttons call) request the one version file and take their release
# out of it through versionFilePick(), so channel, board node and the stable
# fallback cannot differ between them; and no code asks for the release list
# any more. The notes of one tag (releases/tags/) and the download stay.
READERS = {
    "services/update_check.cpp": "runUpdateCheck",
    "services/github_release.cpp": "githubLatestTag",
}
# The list capped ("/releases?per_page="), uncapped ("/releases") and its
# newest entry ("/releases/latest"); the notes of one tag and the download
# (/releases/tags/, /releases/download/) are not the list.
LIST_REQUEST = re.compile(r'"/releases(?:\?|/latest"|")')


def body_of(rel, name):
    """The code (comments and string contents blanked) of the one function called name, or None."""
    text = read(rel)
    if text is None:
        return None
    source = cc.Source(text)
    spans = [(start, end) for found, start, end in cc.find_functions(source.code) if found == name]
    if len(spans) != 1:
        errors.append(f"src/{rel}: {name}() is not defined exactly once")
        return None
    return source.code[spans[0][0] + 1:spans[0][1]]


for rel, name in READERS.items():
    body = body_of(rel, name)
    if body is None:
        continue
    if "versionFilePick(" not in body:
        errors.append(f"src/{rel}: {name}() does not take its release through versionFilePick()")
    text = read(rel)
    if text is not None and "VERSION_FILE_URL" not in cc.Source(text).code:
        errors.append(f"src/{rel}: does not request VERSION_FILE_URL")

for path in sorted(src.rglob("*")):
    if path.suffix not in (".h", ".cpp", ".hpp", ".c", ".ino"):
        continue
    source = cc.Source(path.read_text(encoding="utf-8", errors="replace"))
    for match in LIST_REQUEST.finditer(source.code_str):
        rel = path.relative_to(src).as_posix()
        errors.append(f"src/{rel}:{source.line_of(match.start())}: still asks GitHub for the release list "
                      f"({match.group(0)})")

# 7. The checksum goes with the tag it belongs to, whichever check found it:
# otaSetLatest() sets the tag and its checksum as one pair (otaExpectedSha()
# hands the checksum out only for that tag), and nothing but ota_state.cpp
# writes the pair itself. The daily check's tick, the check on the scale and
# the web page's each set what they found through it, and the web job hands
# the checksum it read on to the page's handler.
OTA_STATE = "services/ota_state.cpp"
PAIR_WRITE = re.compile(r"\b(?:strncpy|strlcpy|snprintf|memcpy|strcpy)\s*\(\s*(gh_latest_sha(?:_tag)?)\b"
                        r"|\b(gh_latest_sha(?:_tag)?)\s*(?:\[[^\]]*\]\s*)?=(?!=)")
SETTERS = {
    "services/update_check.cpp": "updateCheckTick",
    "ui/ota_github.cpp": "doGithubOtaCheck",
    "web/pages/page_firmware.cpp": "ghCheckFinish",
}
ota_header = read("services/ota_state.h")
if ota_header is not None and not re.search(r"\bvoid\s+otaSetLatest\s*\(\s*const\s+char\s*\*\s*\w+\s*,"
                                             r"\s*const\s+char\s*\*\s*\w+\s*\)\s*;", ota_header):
    errors.append("src/services/ota_state.h: no void otaSetLatest(const char *tag, const char *sha);")
for path in sorted(src.rglob("*")):
    if path.suffix not in (".h", ".cpp", ".hpp", ".c", ".ino"):
        continue
    rel = path.relative_to(src).as_posix()
    if rel == OTA_STATE:
        continue
    source = cc.Source(path.read_text(encoding="utf-8", errors="replace"))
    for match in PAIR_WRITE.finditer(source.code):
        errors.append(f"src/{rel}:{source.line_of(match.start())}: writes {match.group(1) or match.group(2)} "
                      "itself, not through otaSetLatest()")
for rel, name in SETTERS.items():
    body = body_of(rel, name)
    if body is not None and "otaSetLatest(" not in body:
        errors.append(f"src/{rel}: {name}() does not set what it found through otaSetLatest()")
gh_job = body_of("web/web_jobs.cpp", "runGhCheck")
if gh_job is not None and not re.search(r"\bs_res\.sha\b", gh_job):
    errors.append("src/web/web_jobs.cpp: runGhCheck() does not hand the checksum on (s_res.sha)")

# The checksum a check by hand read is the one it sets, not an empty one: the
# scale's check hands a buffer to githubLatestTag() and that very buffer to
# otaSetLatest(), and the web page's handler takes the job's r.sha and sets
# it. A literal, a nullptr or another variable there is the bug back.
# githubLatestTag(channel, tag, sizeof(tag), sha, sizeof(sha), ...): group 2.
SHA_BUFFER = re.compile(r"githubLatestTag\(\s*\w+\s*,\s*(\w+)\s*,\s*sizeof\(\s*\1\s*\)\s*,"
                        r"\s*(\w+)\s*,\s*sizeof\(\s*\2\s*\)")
device_check = body_of("ui/ota_github.cpp", "doGithubOtaCheck")
if device_check is not None:
    passed = SHA_BUFFER.search(device_check)
    if not passed:
        errors.append("src/ui/ota_github.cpp: doGithubOtaCheck() does not hand githubLatestTag() a sha buffer")
    elif not re.search(r"otaSetLatest\(\s*\w+\s*,\s*" + re.escape(passed.group(2)) + r"\s*\)", device_check):
        errors.append(f"src/ui/ota_github.cpp: doGithubOtaCheck() does not set the sha it read "
                      f"(otaSetLatest(tag, {passed.group(2)}))")
web_finish = body_of("web/pages/page_firmware.cpp", "ghCheckFinish")
if web_finish is not None:
    copied = re.search(r"\b(?:strncpy|snprintf|strlcpy)\(\s*(\w+)\s*,[^;]*\br\.sha\b", web_finish)
    if not copied:
        errors.append("src/web/pages/page_firmware.cpp: ghCheckFinish() does not take the job's checksum (r.sha)")
    elif not re.search(r"otaSetLatest\(\s*\w+\s*,\s*" + re.escape(copied.group(1)) + r"\s*\)", web_finish):
        errors.append(f"src/web/pages/page_firmware.cpp: ghCheckFinish() does not set the checksum it took "
                      f"(otaSetLatest(tag, {copied.group(1)}))")

# 8. Verdict.
if errors:
    print("update source: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("update source: ok")
