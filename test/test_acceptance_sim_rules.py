"""Compile the rules of the acceptance build's made-up server answer and made-up
tag (Spec 5.3 AA, v0.8.7-beta.2; services/sim_answer_rules.h,
services/sim_tag_rules.h) and check which requests an answer takes, how many,
which arguments the call accepts, and what the empty NTAG215 reads as."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r"""
#include "services/sim_answer_rules.h"
#include "services/sim_tag_rules.h"
#include <cassert>
#include <cstring>

int main() {
  // ---- the arguments of the call
  assert(simAnswerArgsOk(401, 1, "", ""));
  assert(simAnswerArgsOk(429, 3, "spools", "120"));
  assert(simAnswerArgsOk(599, 50, "update-spool-weight", ""));
  assert(!simAnswerArgsOk(200, 1, "", ""));      // not an error: nothing to show
  assert(!simAnswerArgsOk(399, 1, "", ""));
  assert(!simAnswerArgsOk(600, 1, "", ""));
  assert(!simAnswerArgsOk(500, 0, "", ""));
  assert(!simAnswerArgsOk(500, 51, "", ""));
  assert(!simAnswerArgsOk(500, 1, "has space", ""));
  assert(!simAnswerArgsOk(500, 1, "0123456789012345678901234567890123456789", ""));  // longer than the word holds
  assert(!simAnswerArgsOk(429, 1, "", "12.5"));
  assert(!simAnswerArgsOk(429, 1, "", "12345678901"));
  assert(simAnswerArgsOk(429, 1, "", "86400"));

  // ---- which requests it takes, and how many
  SimAnswer a;
  assert(!simAnswerTake(a, "/api/v1/spoolman/inventory/spools"));   // nothing set
  simAnswerSet(a, 500, 2, "update-spool-weight", "");
  assert(!simAnswerTake(a, "/api/v1/spoolman/inventory/spools"));   // another path
  assert(a.left == 2);
  assert(simAnswerTake(a, "/api/v1/spoolbuddy/scale/update-spool-weight"));
  assert(a.left == 1);
  assert(simAnswerTake(a, "/api/v1/spoolbuddy/scale/update-spool-weight?x=1"));
  assert(a.left == 0);
  assert(!simAnswerTake(a, "/api/v1/spoolbuddy/scale/update-spool-weight"));  // used up
  simAnswerSet(a, 429, 1, "", "120");
  assert(simAnswerTake(a, "/anything") && a.code == 429 && strcmp(a.retry, "120") == 0);
  assert(!simAnswerTake(a, "/anything"));
  simAnswerSet(a, 401, 5, "", "");
  simAnswerClear(a);
  assert(!simAnswerTake(a, "/anything"));
  assert(!simAnswerTake(a, nullptr));

  // ---- the made-up tag: a 7 byte UID, an empty NTAG215
  const uint8_t uid[7] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
  assert(simTagUidOk("04A1B2C3D4E5F6"));
  assert(!simTagUidOk("04A1B2C3D4E5"));        // 6 bytes
  assert(!simTagUidOk("04A1B2C3D4E5F6G0"));
  assert(!simTagUidOk("04A1B2C3D4E5FG"));
  uint8_t got[7] = {0};
  assert(simTagUidParse("04a1b2c3d4e5f6", got) && memcmp(got, uid, 7) == 0);

  uint8_t p[4];
  assert(simNtagPage(uid, 0, p));
  assert(p[0] == 0x04 && p[1] == 0xA1 && p[2] == 0xB2 && p[3] == (uint8_t)(0x88 ^ 0x04 ^ 0xA1 ^ 0xB2));
  assert(simNtagPage(uid, 1, p));
  assert(p[0] == 0xC3 && p[1] == 0xD4 && p[2] == 0xE5 && p[3] == 0xF6);
  assert(simNtagPage(uid, 2, p));
  assert(p[0] == (uint8_t)(0xC3 ^ 0xD4 ^ 0xE5 ^ 0xF6));
  assert(simNtagPage(uid, 3, p));            // capability container: NDEF, 496 bytes
  assert(p[0] == 0xE1 && p[1] == 0x10 && p[2] == 0x3E && p[3] == 0x00);
  assert(simNtagPage(uid, 4, p));            // an empty NDEF message, then the end
  assert(p[0] == 0x03 && p[1] == 0x00 && p[2] == 0xFE && p[3] == 0x00);
  assert(simNtagPage(uid, 5, p) && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0);
  assert(simNtagPage(uid, 129, p) && p[0] == 0);
  assert(simNtagPage(uid, 134, p));
  assert(!simNtagPage(uid, 135, p));         // past the end of an NTAG215
  return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("acceptance sim rules: ok")
