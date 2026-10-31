"""Compile the production version comparison and check that it ranks the tags of this fork in the order the release workflow assumes (scripts/release_files.py ranks them the same way): v0.8.3-beta.87 < v0.8.4-beta.1 < v0.8.4-beta.2 < v0.8.4 < v0.8.5-beta.1, the original's v0.8.3 below the fork's first pre-release, and a beta counter outside 1..998 ranking like the bare -beta."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/version_compare.h"
#include <cassert>

int main() {
  // The series the fork releases. v0.8.3-beta.87 is the last build of the
  // base this fork started from, v0.8.4-beta.1 its first pre-release.
  assert(parseVersion("v0.8.3-beta.87") < parseVersion("v0.8.4-beta.1"));
  assert(parseVersion("v0.8.4-beta.1") < parseVersion("v0.8.4-beta.2"));
  assert(parseVersion("v0.8.4-beta.2") < parseVersion("v0.8.4"));
  assert(parseVersion("v0.8.4") < parseVersion("v0.8.5-beta.1"));

  // The original's release today is v0.8.3: the fork's first pre-release is
  // already newer, so a scale on the original's firmware is offered it.
  assert(parseVersion("v0.8.4-beta.1") > parseVersion("v0.8.3"));

  // A counter of 999 is out of range and ranks like the bare "-beta",
  // not like the final release (which is 999 on its own).
  assert(parseVersion("v0.8.3-beta.999") == parseVersion("v0.8.3-beta"));

  // The history described in version_compare.cpp: the public -beta, then the
  // counters after it, then the next public -beta.
  assert(parseVersion("v0.5.7-beta") < parseVersion("v0.5.7-beta.1"));
  assert(parseVersion("v0.5.7-beta.2") < parseVersion("v0.5.8-beta"));
  assert(parseVersion("v0.8.3-beta.998") < parseVersion("v0.8.3"));

  // The exact numbers, the same table as in test_release_files.py: the
  // release workflow ranks tags with Python, and the two must agree on every
  // one of these or a device and the workflow disagree about what is newer.
  assert(parseVersion("v0.8.4") == 8004999ULL);
  assert(parseVersion("v0.8.4-beta.1") == 8004001ULL);
  assert(parseVersion("v0.8.3-beta") == 8003000ULL);
  assert(parseVersion("v0.8.3-beta.998") == 8003998ULL);
  assert(parseVersion("v0.8.3-beta.0") == 8003000ULL);
  assert(parseVersion("v0.8.3-beta.999") == 8003000ULL);
  assert(parseVersion("v0.8.3-beta.5000") == 8003000ULL);
  assert(parseVersion("v1.2.3") == 1002003999ULL);
  assert(parseVersion("1.2.3") == 1002003999ULL);
  // The documentation build (feat/doku-bilder, never released) names itself
  // with "-doku" and ranks one below the version it was built on, so the
  // update check offers the real release again.
  assert(parseVersion("v0.8.6-beta.12-doku") < parseVersion("v0.8.6-beta.12"));
  assert(parseVersion("v0.8.6-beta.12-doku") == parseVersion("v0.8.6-beta.11"));
  assert(parseVersion("v0.8.6-doku") < parseVersion("v0.8.6"));

  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "version_compare.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("version compare: ok")
