"""Compile the production board scan (src/services/board_image.cpp) against a stub of the board header and check which images a board takes: its own line, another board's line (final, but the board's own line after it settles it), no line (only the board that had no line to carry), a line that arrives split across two feeds, and what is no line at all (a name that is cut off at the end, a name of more than 40 bytes). It also checks that a new upload starts from nothing. This is the scan the browser upload and the GitHub download both read an image with, so that a scale can never install another board's image."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]

# The two things board_image.cpp takes from the board header: its id, and the
# node of its version.json, which is empty for the board whose images never
# had a node and therefore never had a line (the WT32). Both are spelled out
# here on purpose: the production headers pull in the whole board, and the
# values are what the real board_config.h files define.
STUB = r'''#pragma once
// Host test stub of boards/board.h.
#define BOARD_ID "%(id)s"
#define BOARD_VERSION_NODE %(node)s
// board_image.cpp names a constant NAME_MAX; some C libraries define a macro
// of that name in <limits.h> (the length of a file name), which would turn
// the constant into a syntax error.
#undef NAME_MAX
'''

# What both programs share: the lines as the image carries them, and three
# ways to hand the bytes to the scan (in one piece, cut in two at any point,
# one byte at a time).
COMMON = r'''
#include "services/board_image.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// One line as the image carries it: the prefix, the name, the line end.
#define IMG_LINE(name) "SpoolmanScale-board:" name "\n"
#define NAME_10 "xxxxxxxxxx"
#define NAME_40 NAME_10 NAME_10 NAME_10 NAME_10
#define NAME_41 NAME_40 "x"

[[maybe_unused]] static BoardImageVerdict feed(const char* s) {
  return boardImageScanFeed(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

// The text cut in two after `at` bytes: the verdict after the second part.
[[maybe_unused]] static BoardImageVerdict feedSplit(const char* s, size_t at) {
  const size_t n = strlen(s);
  assert(at <= n);
  boardImageScanFeed(reinterpret_cast<const uint8_t*>(s), at);
  return boardImageScanFeed(reinterpret_cast<const uint8_t*>(s) + at, n - at);
}

// One byte at a time, the shortest chunks a download can deliver.
[[maybe_unused]] static BoardImageVerdict feedBytes(const char* s) {
  BoardImageVerdict v = BOARD_IMAGE_UNMARKED;
  for (size_t i = 0; s[i] != 0; i++)
    v = boardImageScanFeed(reinterpret_cast<const uint8_t*>(s) + i, 1);
  return v;
}

// Every byte value once, which is what code and data look like to the scan.
// It contains the first letter of the prefix and the line end, but not the
// prefix.
[[maybe_unused]] static BoardImageVerdict feedNoise() {
  uint8_t noise[256];
  for (int i = 0; i < 256; i++) noise[i] = static_cast<uint8_t>(i);
  return boardImageScanFeed(noise, sizeof(noise));
}
'''

# A build for the Waveshare 7B: it has a node of its own in version.json, so
# an image without a line is not one it takes.
MAIN_7B = r'''
#define OWN_ID "waveshare-s3-lcd7b"

int main() {
  // The line this build carries, as the boot log prints it.
  assert(strcmp(boardImageMarker(), "SpoolmanScale-board:" OWN_ID) == 0);

  // Its own line: taken, wherever it stands.
  boardImageScanBegin();
  assert(feed("some code, then " IMG_LINE(OWN_ID) "and some more code") == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed(IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  boardImageScanBegin();
  assert(feedNoise() == BOARD_IMAGE_UNMARKED);
  assert(feed(IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());

  // Another board's line: FOREIGN, and not taken. The names that start like
  // this board's, or go on after it, are other boards.
  boardImageScanBegin();
  assert(feed("code " IMG_LINE("wt32-sc01-plus")) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed(IMG_LINE("waveshare-s3-lcd7")) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed(IMG_LINE(OWN_ID "b")) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed(IMG_LINE("Waveshare-s3-lcd7b")) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());
  // A line with no name is the line of no board.
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board:\n") == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());

  // The own line after a foreign one settles it, as the scan says: FOREIGN
  // is what the caller sees after the first line and can drop the image on
  // the spot, but an image that carries both ends up taken if it is read on.
  boardImageScanBegin();
  assert(feed(IMG_LINE("wt32-sc01-plus")) == BOARD_IMAGE_FOREIGN);
  assert(feed("code " IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  // And the other way round the own line stays.
  boardImageScanBegin();
  assert(feed(IMG_LINE(OWN_ID) "code " IMG_LINE("wt32-sc01-plus")) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());

  // No line: this board never had an image without one, so it is not taken.
  boardImageScanBegin();
  assert(!boardImageScanAccepts());
  assert(feed("just code, no line in it") == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feedNoise() == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());

  // The line may arrive in any pieces: cut at every point, and byte by byte.
  const char* own_text = "ab" IMG_LINE(OWN_ID) "cd";
  const char* foreign_text = "ab" IMG_LINE("wt32-sc01-plus") "cd";
  for (size_t at = 0; at <= strlen(own_text); at++) {
    boardImageScanBegin();
    assert(feedSplit(own_text, at) == BOARD_IMAGE_MATCH);
    assert(boardImageScanAccepts());
  }
  for (size_t at = 0; at <= strlen(foreign_text); at++) {
    boardImageScanBegin();
    assert(feedSplit(foreign_text, at) == BOARD_IMAGE_FOREIGN);
    assert(!boardImageScanAccepts());
  }
  boardImageScanBegin();
  assert(feedBytes(own_text) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  boardImageScanBegin();
  assert(feedBytes(foreign_text) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());

  // What is no line. The prefix and a name that stops at the end of the
  // data, whatever it names: no line, no verdict.
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board:" OWN_ID) == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board:") == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());
  // A foreign name cut off at the end does not count against an own line.
  boardImageScanBegin();
  assert(feed(IMG_LINE(OWN_ID) "SpoolmanScale-board:wt32-sc01-plus") == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  // A name of 40 bytes is the longest a line holds; 41 is no line, and the
  // scan reads on behind it.
  boardImageScanBegin();
  assert(feed(IMG_LINE(NAME_40)) == BOARD_IMAGE_FOREIGN);
  boardImageScanBegin();
  assert(feed(IMG_LINE(NAME_41)) == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed(IMG_LINE(NAME_41) IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  // The prefix and then more than 40 bytes without a line end.
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board:" NAME_40 NAME_10) == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());
  // The words of the prefix elsewhere make no line, and a failed prefix
  // directly in front of a line does not hide it.
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board SpoolmanScale-board;x\n") == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board" IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed("SSpoolmanScale-board:" OWN_ID "\n") == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());

  // A new upload starts from nothing: neither a verdict nor half a line of
  // the one before is carried over.
  boardImageScanBegin();
  assert(feed(IMG_LINE("wt32-sc01-plus")) == BOARD_IMAGE_FOREIGN);
  boardImageScanBegin();
  assert(!boardImageScanAccepts());
  assert(feed("code") == BOARD_IMAGE_UNMARKED);
  boardImageScanBegin();
  assert(feed(IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  boardImageScanBegin();
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed("SpoolmanScale-bo") == BOARD_IMAGE_UNMARKED);
  boardImageScanBegin();
  assert(feed("ard:" OWN_ID "\n") == BOARD_IMAGE_UNMARKED);
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board:waveshare-s3-") == BOARD_IMAGE_UNMARKED);
  boardImageScanBegin();
  assert(feed("lcd7b\n") == BOARD_IMAGE_UNMARKED);
  assert(!boardImageScanAccepts());
  return 0;
}
'''

# A build for the WT32: its images never had a node in version.json and,
# before the line existed, never a line either, so an image without one is
# still the board's own. The line of another board is not.
MAIN_WT32 = r'''
#define OWN_ID "wt32-sc01-plus"

int main() {
  assert(strcmp(boardImageMarker(), "SpoolmanScale-board:" OWN_ID) == 0);

  // No line: an image from before there was one, taken.
  boardImageScanBegin();
  assert(boardImageScanAccepts());
  assert(feed("just code, no line in it") == BOARD_IMAGE_UNMARKED);
  assert(boardImageScanAccepts());
  boardImageScanBegin();
  assert(feedNoise() == BOARD_IMAGE_UNMARKED);
  assert(boardImageScanAccepts());

  // Its own line: taken, in any pieces.
  boardImageScanBegin();
  assert(feed("code " IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());
  const char* own_text = "ab" IMG_LINE(OWN_ID) "cd";
  for (size_t at = 0; at <= strlen(own_text); at++) {
    boardImageScanBegin();
    assert(feedSplit(own_text, at) == BOARD_IMAGE_MATCH);
    assert(boardImageScanAccepts());
  }
  boardImageScanBegin();
  assert(feedBytes(own_text) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());

  // Another board's line is not taken, although an image without any is.
  boardImageScanBegin();
  assert(feed("code " IMG_LINE("waveshare-s3-lcd7b")) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());
  boardImageScanBegin();
  assert(feed(IMG_LINE("waveshare-s3-lcd7")) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());
  const char* foreign_text = "ab" IMG_LINE("waveshare-s3-lcd7b") "cd";
  for (size_t at = 0; at <= strlen(foreign_text); at++) {
    boardImageScanBegin();
    assert(feedSplit(foreign_text, at) == BOARD_IMAGE_FOREIGN);
    assert(!boardImageScanAccepts());
  }
  boardImageScanBegin();
  assert(feedBytes(foreign_text) == BOARD_IMAGE_FOREIGN);
  assert(!boardImageScanAccepts());

  // The own line after a foreign one settles it.
  boardImageScanBegin();
  assert(feed(IMG_LINE("waveshare-s3-lcd7b")) == BOARD_IMAGE_FOREIGN);
  assert(feed(IMG_LINE(OWN_ID)) == BOARD_IMAGE_MATCH);
  assert(boardImageScanAccepts());

  // A name cut off at the end is no line: no verdict, and an image without a
  // line is this board's to take.
  boardImageScanBegin();
  assert(feed("SpoolmanScale-board:waveshare-s3-lcd7b") == BOARD_IMAGE_UNMARKED);
  assert(boardImageScanAccepts());

  // A new upload starts from nothing.
  boardImageScanBegin();
  assert(feed(IMG_LINE("waveshare-s3-lcd7b")) == BOARD_IMAGE_FOREIGN);
  boardImageScanBegin();
  assert(boardImageScanAccepts());
  assert(feed("code") == BOARD_IMAGE_UNMARKED);
  return 0;
}
'''

BUILDS = [
    # (name, board id, version.json node as the board header spells it, main)
    ("7B", "waveshare-s3-lcd7b", "BOARD_ID", MAIN_7B),
    ("WT32", "wt32-sc01-plus", '""', MAIN_WT32),
]

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    for name, board_id, node, main in BUILDS:
        # The stub's folder comes before src/ on the include path, so that
        # "boards/board.h" in the production file is found here first.
        stub = tmp / f"stub_{name}"
        (stub / "boards").mkdir(parents=True)
        (stub / "boards" / "board.h").write_text(STUB % {"id": board_id, "node": node})
        cpp = tmp / f"t_{name}.cpp"
        binary = tmp / f"t_{name}"
        cpp.write_text(COMMON + main)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(stub), "-I", str(root / "src"),
                        str(cpp), str(root / "src" / "services" / "board_image.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print("board image: ok")
