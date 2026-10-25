"""Read the source tree without a compiler and check that every way an image gets into the flash first looks at the board line it carries (src/services/board_image.h), so that a scale can never install another board's image. There are two ways: the upload in the browser and the download from GitHub. Both begin the scan, feed it the bytes that go into the flash, drop the image on a foreign verdict and ask whether this board accepts the image before it is committed. The GitHub download is the one that used to rely on a checksum alone, and that checksum comes from the same workflow that names the files, so a file put under the wrong board's name carries a matching sum: the scan stops the loop on a foreign line, the refusal block names the reason before Update.end(), and the no-line case is refused where the board requires a line, with or without a published checksum. A new place that commits an image (Update.end) must be added here and use the scan too."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer and the function finder of the convention ratchet: comments
# and string contents blanked (offsets kept), function bodies by brace
# matching. The script is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

# The only two files that commit an image to the flash. A third one has to
# be named here, and then it is held to the same calls.
FLASH_FILES = ["services/github_release.cpp", "web/pages/page_firmware.cpp"]
BROWSER = "web/pages/page_firmware.cpp"
GITHUB = "services/github_release.cpp"
GITHUB_FUNCTION = "githubFlashTag"

SCAN_CALLS = ["boardImageScanBegin()", "boardImageScanFeed(", "boardImageScanAccepts()"]
UPDATE_END = re.compile(r"\bUpdate\s*\.\s*end\s*\(")
UPDATE_WRITE = re.compile(r"\bUpdate\s*\.\s*write\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
FEED_FOREIGN = re.compile(
    r"\bboardImageScanFeed\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*==\s*BOARD_IMAGE_FOREIGN\s*\)\s*\{([^{}]*)\}")
BROWSER_FEED = re.compile(r"\bboardImageScanFeed\s*\([^;{}]*\)\s*==\s*BOARD_IMAGE_FOREIGN\b")
BROWSER_ACCEPTS = re.compile(r"\bif\s*\(\s*!\s*boardImageScanAccepts\(\)\s*\)")

errors = []


def load(rel):
    path = src / rel
    if not path.is_file():
        errors.append(f"src/{rel}: file is missing")
        return None
    return cc.Source(path.read_text(encoding="utf-8", errors="replace"))


def depth_at(code, start, offset):
    """How many braces are open at offset, counted from start."""
    return code.count("{", start, offset) - code.count("}", start, offset)


def block_after(code, offset):
    """The text between the braces of the block that opens after offset."""
    begin = code.find("{", offset)
    if begin < 0:
        return None
    depth = 0
    for index in range(begin, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[begin + 1:index]
    return None


# 1. Who commits an image to the flash: exactly the two known files, and each
# of them uses all three calls of the scan.
committers = sorted(path.relative_to(src).as_posix() for path in src.rglob("*.cpp")
                    if UPDATE_END.search(cc.Source(path.read_text(encoding="utf-8", errors="replace")).code))
if committers != sorted(FLASH_FILES):
    errors.append(f"src: Update.end() is called in {committers}, this test knows {sorted(FLASH_FILES)}: "
                  "a new way of flashing an image needs the board scan and a place in FLASH_FILES")
sources = {rel: load(rel) for rel in FLASH_FILES}
for rel, source in sources.items():
    if source is None:
        continue
    for call in SCAN_CALLS:
        if call not in source.code:
            errors.append(f"src/{rel}: does not call {call}")

# 2. The upload in the browser: the scan begins with the upload, reads each
# chunk and drops a foreign image on the spot, and the end of the upload asks
# whether this board accepts the image before Update.end() commits it.
browser = sources[BROWSER]
if browser is not None:
    code = browser.code
    begin = code.find(SCAN_CALLS[0])
    feed = BROWSER_FEED.search(code)
    accepts = BROWSER_ACCEPTS.search(code)
    end = UPDATE_END.search(code)
    if feed is None:
        errors.append(f"src/{BROWSER}: a chunk is not compared with BOARD_IMAGE_FOREIGN")
    if accepts is None:
        errors.append(f"src/{BROWSER}: the end of the upload does not refuse on !boardImageScanAccepts()")
    if None not in (feed, accepts, end) and begin >= 0:
        if not begin < feed.start() < accepts.start() < end.start():
            errors.append(f"src/{BROWSER}: the scan must begin, then read the chunks, then be asked, "
                          "all before Update.end()")
        refusal = block_after(code, accepts.end())
        if refusal is None or not re.search(r"\bUpdate\s*\.\s*abort\s*\(", refusal):
            errors.append(f"src/{BROWSER}: the refusal on !boardImageScanAccepts() does not abort the update")

# 3. The download from GitHub, githubFlashTag(): the same, inside the one
# function that writes the image.
github = sources[GITHUB]
if github is not None:
    code = github.code
    spans = [(start, end) for name, start, end in cc.find_functions(code) if name == GITHUB_FUNCTION]
    if len(spans) != 1:
        errors.append(f"src/{GITHUB}: {GITHUB_FUNCTION}() is not defined exactly once")
    else:
        base = spans[0][0] + 1
        body = code[base:spans[0][1]]
        where = f"src/{GITHUB}: {GITHUB_FUNCTION}()"

        write = UPDATE_WRITE.search(body)
        feed = FEED_FOREIGN.search(body)
        begin = body.find(SCAN_CALLS[0])
        loops = [m for m in re.finditer(r"\b(?:while|for)\s*\(", body) if write and m.start() < write.start()]
        if write is None:
            errors.append(f"{where}: does not call Update.write(buffer, length)")
        if begin < 0:
            errors.append(f"{where}: does not call {SCAN_CALLS[0]}")
        if feed is None:
            errors.append(f"{where}: does not compare boardImageScanFeed(buffer, length) with BOARD_IMAGE_FOREIGN "
                          "in an if with a block")
        if write and feed and begin >= 0 and loops:
            loop = loops[-1].start()
            # The scan restarts for every download and sees only what went
            # into the flash: begun before the loop, fed after the write,
            # with the very bytes that were written.
            if not begin < loop:
                errors.append(f"{where}: {SCAN_CALLS[0]} must come before the write loop")
            if not write.start() < feed.start():
                errors.append(f"{where}: the scan must be fed after Update.write(), with the bytes that went in")
            if write.groups() != feed.groups()[:2]:
                errors.append(f"{where}: Update.write({', '.join(write.groups())}) and "
                              f"boardImageScanFeed({', '.join(feed.groups()[:2])}) are not given the same bytes")
            if depth_at(body, 0, write.start()) != depth_at(body, 0, feed.start()):
                errors.append(f"{where}: the scan is not fed in the same block as the write")
            # A foreign verdict ends the download at once, like the browser
            # upload: the rest of a wrong image is not written to the flash.
            stop = feed.group(3)
            flag = re.search(r"\b(\w+)\s*=\s*true\s*;", stop)
            if not re.search(r"\bbreak\s*;", stop):
                errors.append(f"{where}: a foreign verdict does not end the download loop (break)")
            if flag is None:
                errors.append(f"{where}: a foreign verdict does not set a flag for the refusal")
            else:
                name = flag.group(1)
                if not re.search(r"\bbool\s+" + name + r"\s*=\s*false\s*;", body[:loop]):
                    errors.append(f"{where}: the flag {name} is not declared false before the loop")

                # The refusal chain: foreign first (a download stopped on a
                # foreign line is also incomplete, and "Incomplete" would hide
                # why), the missing line last, both in the chain that sets
                # `fail`, at the level of the first test (not inside the
                # branch of the checksum, which a download without a
                # published sum never enters).
                first = re.search(r"\bif\s*\(\s*write_failed\s*\)", body)
                chain = first.start() if first else -1
                incomplete = re.search(r"\bdone\s*!=\s*total\b", body)
                foreign = re.search(r"\belse\s+if\s*\(\s*" + name + r"\s*\)\s*\{?\s*fail\s*=", body)
                missing = re.search(r"\belse\s+if\s*\(\s*!\s*boardImageScanAccepts\(\)\s*\)\s*\{?\s*fail\s*=", body)
                commit = UPDATE_END.search(body)
                refuse = re.search(r"\bif\s*\(\s*fail\s*\)", body)
                if chain < 0 or incomplete is None or commit is None or refuse is None:
                    errors.append(f"{where}: the chain of refusals (write_failed, done != total, if (fail), "
                                  "Update.end) is not there to be checked")
                else:
                    if foreign is None:
                        errors.append(f"{where}: no `else if ({name}) fail = ...` for a foreign image")
                    elif not chain < foreign.start() < incomplete.start():
                        errors.append(f"{where}: the foreign image must be refused before the incomplete download "
                                      "(it ends the loop early and would be reported as incomplete)")
                    if missing is None:
                        errors.append(f"{where}: no `else if (!boardImageScanAccepts()) fail = ...` "
                                      "for an image without a board line")
                    elif not incomplete.start() < missing.start() < refuse.start() < commit.start():
                        errors.append(f"{where}: the board line must be asked after the download is known to be "
                                      "complete and before the refusal block and Update.end()")
                    for label, match in (("foreign", foreign), ("missing line", missing)):
                        if match is not None and depth_at(body, 0, match.start()) != depth_at(body, 0, chain):
                            errors.append(f"{where}: the {label} test is nested in another branch, "
                                          "it can be skipped")
                    # Whatever sets `fail` ends in the refusal block, which
                    # aborts the update and leaves before anything is committed.
                    refusal = block_after(body, refuse.end())
                    if refusal is None or not re.search(r"\bUpdate\s*\.\s*abort\s*\(", refusal) \
                            or not re.search(r"\breturn\s+false\s*;", refusal):
                        errors.append(f"{where}: the block of `if (fail)` does not abort the update and return false")

# 4. Verdict.
if errors:
    print("board image paths: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("board image paths: ok")
