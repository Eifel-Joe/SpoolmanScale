"""Compile the production rules of the connection test for a Klipper printer (services/klipper_test_rules.h) and check them: when two addresses are the same Spoolman, how the newest spool of each says whether the databases are the same, and the verdict in the order of the specification, one step at a time. A printer on another Spoolman than the scale's must be found here, because assigning a spool there would write a number that means another spool."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/klipper_test_rules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* g_what = "";
#define WHAT(s) (g_what = (s))
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s] line %d: %s\n", g_what, __LINE__, #c); abort(); } } while (0)
#define CHECK_INT(a, b) do { const long long x_ = (long long)(a); const long long y_ = (long long)(b); \
  if (x_ != y_) { fprintf(stderr, "FAIL [%s] line %d: %s is %lld, expected %lld\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)
#define CHECK_STR(a, b) do { const char* x_ = (a); const char* y_ = (b); \
  if (strcmp(x_, y_) != 0) { fprintf(stderr, "FAIL [%s] line %d: %s is \"%s\", expected \"%s\"\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)

// The probe of a good test, built with the numbers as plain ints. Each case
// below turns one thing of it, or two, to see which the verdict names.
static KlipperProbe good() {
  KlipperProbe p{};
  p.access_code = 200;
  p.info_code = 200;
  snprintf(p.klippy_state, sizeof(p.klippy_state), "%s", "ready");
  p.has_spoolman = true;
  p.status_code = 200;
  p.sm_connected = true;
  p.addr_same = true;
  p.newest = KS_UNKNOWN;
  return p;
}

static void expectAddr(const char* in, const char* want) {
  char out[96];
  memset(out, 0xA5, sizeof(out));
  const bool ok = klipperSpoolmanAddr(in, out, sizeof(out));
  if (!ok) {
    fprintf(stderr, "FAIL [%s]: no address from \"%s\"\n", g_what, in);
    abort();
  }
  CHECK_STR(out, want);
}

int main() {
  WHAT("numbers that are stored");
  CHECK_INT(KS_UNKNOWN, 0);
  CHECK_INT(KS_SAME, 1);
  CHECK_INT(KS_OTHER, 2);

  WHAT("an address, the way it is compared");
  expectAddr("http://192.168.10.178:7912", "http://192.168.10.178:7912");
  expectAddr("192.168.10.178:7912/", "http://192.168.10.178:7912");
  expectAddr("HTTP://192.168.10.178:7912/api", "http://192.168.10.178:7912");
  expectAddr("http://192.168.10.178:7912/", "http://192.168.10.178:7912");
  expectAddr("http://spoolman", "http://spoolman:80");
  expectAddr("spoolman", "http://spoolman:80");
  expectAddr("https://Spoolman.Local/", "https://spoolman.local:443");
  expectAddr("https://x:8443/api", "https://x:8443");
  {
    char out[96];
    CHECK(!klipperSpoolmanAddr("", out, sizeof(out)));
    CHECK(!klipperSpoolmanAddr("http://", out, sizeof(out)));
    // The result must fit with its terminator.
    CHECK(!klipperSpoolmanAddr("http://192.168.10.178:7912", out, 8));
    CHECK(klipperSpoolmanAddr("http://192.168.10.178:7912", out, 27));
    CHECK(!klipperSpoolmanAddr("http://192.168.10.178:7912", out, 26));
  }

  WHAT("the same address, however it was written");
  CHECK(klipperSameAddr("http://192.168.10.178:7912", "192.168.10.178:7912/"));
  CHECK(klipperSameAddr("http://192.168.10.178:7912", "HTTP://192.168.10.178:7912/api"));
  CHECK(klipperSameAddr("192.168.10.178:7912/", "HTTP://192.168.10.178:7912/api"));
  CHECK(klipperSameAddr("http://spoolman", "spoolman:80"));
  CHECK(klipperSameAddr("https://Spoolman.Local", "https://spoolman.local:443/"));
  CHECK(!klipperSameAddr("http://192.168.10.178:7912", "http://192.168.10.178:7913"));
  CHECK(!klipperSameAddr("http://192.168.10.178:7912", "http://192.168.10.179:7912"));
  CHECK(!klipperSameAddr("http://spoolman", "https://spoolman"));
  CHECK(!klipperSameAddr("http://spoolman", "http://spoolman:7912"));
  // The same machine under another name is not told apart by the address
  // alone, which is what the newest spool is for.
  CHECK(!klipperSameAddr("http://spoolman:7912", "http://192.168.10.178:7912"));
  // An address that is none is the same as nothing, not even as itself:
  // "same" opens assigning, so it is never said on nothing.
  CHECK(!klipperSameAddr("", ""));
  CHECK(!klipperSameAddr("", "http://192.168.10.178:7912"));
  CHECK(!klipperSameAddr("http://192.168.10.178:7912", ""));

  WHAT("the newest spool of each: the same database?");
  CHECK_INT(klipperSameNewest(0, "", 0, ""), KS_SAME);                  // both empty
  CHECK_INT(klipperSameNewest(0, "", 0, "2026-10-20T08:15:30Z"), KS_SAME);
  CHECK_INT(klipperSameNewest(0, "", 87, "2026-10-20T08:15:30Z"), KS_OTHER);   // one is empty
  CHECK_INT(klipperSameNewest(87, "2026-10-20T08:15:30Z", 0, ""), KS_OTHER);
  CHECK_INT(klipperSameNewest(87, "2026-10-20T08:15:30Z", 87, "2026-10-20T08:15:30Z"), KS_SAME);
  CHECK_INT(klipperSameNewest(87, "2026-10-20T08:15:30Z", 88, "2026-10-20T08:15:30Z"), KS_OTHER);   // another number
  CHECK_INT(klipperSameNewest(87, "2026-10-20T08:15:30Z", 87, "2026-10-21T09:00:00Z"), KS_OTHER);   // another time
  CHECK_INT(klipperSameNewest(-1, "", 87, "2026-10-20T08:15:30Z"), KS_UNKNOWN);                     // not read
  CHECK_INT(klipperSameNewest(87, "2026-10-20T08:15:30Z", -1, ""), KS_UNKNOWN);
  CHECK_INT(klipperSameNewest(-1, "", -1, ""), KS_UNKNOWN);
  CHECK_INT(klipperSameNewest(-1, "", 0, ""), KS_UNKNOWN);
  CHECK_INT(klipperSameNewest(0, "", -1, ""), KS_UNKNOWN);

  WHAT("the verdict: the good case, and the one that is good by another road");
  {
    KlipperProbe p = good();
    CHECK_INT(klipperTestVerdict(p), KT_OK);
    // Another name, the same database: the newest spools agree.
    p.addr_same = false;
    p.newest = KS_SAME;
    CHECK_INT(klipperTestVerdict(p), KT_OK);
    // The same address wins over a newest spool that differs (a spool added
    // in between).
    p = good();
    p.newest = KS_OTHER;
    CHECK_INT(klipperTestVerdict(p), KT_OK);
  }

  WHAT("the verdict: not reachable");
  {
    KlipperProbe p = good();
    p.access_code = 0;
    CHECK_INT(klipperTestVerdict(p), KT_UNREACHABLE);
    p.access_code = -1;
    CHECK_INT(klipperTestVerdict(p), KT_UNREACHABLE);
    // First of all, whatever else is wrong.
    p.info_code = 401;
    p.has_spoolman = false;
    p.sm_connected = false;
    p.addr_same = false;
    p.newest = KS_OTHER;
    snprintf(p.klippy_state, sizeof(p.klippy_state), "%s", "error");
    CHECK_INT(klipperTestVerdict(p), KT_UNREACHABLE);
    // Any answer at all is "reachable": the next steps judge it.
    p = good();
    p.access_code = 500;
    CHECK_INT(klipperTestVerdict(p), KT_OK);
    p.access_code = 404;
    CHECK_INT(klipperTestVerdict(p), KT_OK);
  }

  WHAT("the verdict: access refused, or an answer that is none");
  {
    KlipperProbe p = good();
    p.info_code = 401;
    CHECK_INT(klipperTestVerdict(p), KT_ACCESS);
    p.info_code = 403;
    CHECK_INT(klipperTestVerdict(p), KT_ACCESS);
    // Before the printer's state is looked at, and before Spoolman.
    p.has_spoolman = false;
    snprintf(p.klippy_state, sizeof(p.klippy_state), "%s", "startup");
    CHECK_INT(klipperTestVerdict(p), KT_ACCESS);
    const int other[4] = {0, 404, 500, 302};
    for (int i = 0; i < COUNT(other); i++) {
      p = good();
      p.info_code = other[i];
      CHECK_INT(klipperTestVerdict(p), KT_BAD_ANSWER);
    }
    p = good();
    p.info_code = 500;
    p.has_spoolman = false;
    CHECK_INT(klipperTestVerdict(p), KT_BAD_ANSWER);
  }

  WHAT("the verdict: Klipper is not ready");
  {
    const char* const states[5] = {"startup", "error", "shutdown", "disconnected", ""};
    for (int i = 0; i < COUNT(states); i++) {
      KlipperProbe p = good();
      snprintf(p.klippy_state, sizeof(p.klippy_state), "%s", states[i]);
      CHECK_INT(klipperTestVerdict(p), KT_KLIPPY);
      // Ahead of Spoolman.
      p.has_spoolman = false;
      CHECK_INT(klipperTestVerdict(p), KT_KLIPPY);
    }
  }

  WHAT("the verdict: no Spoolman in Moonraker");
  {
    KlipperProbe p = good();
    p.has_spoolman = false;
    CHECK_INT(klipperTestVerdict(p), KT_NO_SPOOLMAN);
    // Ahead of Spoolman being offline: its status was never asked.
    p.status_code = 404;
    p.sm_connected = false;
    CHECK_INT(klipperTestVerdict(p), KT_NO_SPOOLMAN);
  }

  WHAT("the verdict: Spoolman not connected");
  {
    const int codes[4] = {0, 404, 500, 503};
    for (int i = 0; i < COUNT(codes); i++) {
      KlipperProbe p = good();
      p.status_code = codes[i];
      CHECK_INT(klipperTestVerdict(p), KT_SM_OFFLINE);
    }
    KlipperProbe p = good();
    p.sm_connected = false;
    CHECK_INT(klipperTestVerdict(p), KT_SM_OFFLINE);
    // Ahead of the question which Spoolman it is.
    p.addr_same = false;
    p.newest = KS_OTHER;
    CHECK_INT(klipperTestVerdict(p), KT_SM_OFFLINE);
  }

  WHAT("the verdict: another Spoolman than the scale's");
  {
    KlipperProbe p = good();
    p.addr_same = false;
    p.newest = KS_OTHER;
    CHECK_INT(klipperTestVerdict(p), KT_OTHER_SPOOLMAN);
    // Not being able to tell is not "the same".
    p.newest = KS_UNKNOWN;
    CHECK_INT(klipperTestVerdict(p), KT_OTHER_SPOOLMAN);
    p.newest = KS_SAME;
    CHECK_INT(klipperTestVerdict(p), KT_OK);
  }
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "klipper_test_rules.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("test_klipper_test_rules: ok")
