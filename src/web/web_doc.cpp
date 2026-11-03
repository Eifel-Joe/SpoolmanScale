#include "web/web_doc.h"

#if defined(SSC_ACCEPTANCE)

#include <stdint.h>
#include <stdlib.h>

#include "app/doc_build.h"
#include "hardware/battery_diag.h"
#include "hardware/sd_logger.h"
#include "web/web_access.h"
#include "lang.h"

// The routes of the acceptance and documentation builds (SSC_ACCEPTANCE, never
// in a release), behind the maintenance gate like the screenshots they serve:
//   POST /api/touch?x=&y=[&ms=][&x2=&y2=]  touch the display (480x320 coordinates)
//   POST /api/sim/weight?g=<grams>|off      a simulated weight on the pad,
//                                           which ends by itself after 15 minutes
// This file is the only place in src/ that spells them: the release run looks
// for these two texts in every file it attaches, and a hit means a switch got
// into a release (test_build_variants.py keeps the texts out of every other file).

// An argument as a whole number, def when it is missing.
static int intArg(WebServer& srv, const char* name, int def) {
  if (!srv.hasArg(name)) return def;
  const String v = srv.arg(name);
  char* end = nullptr;
  const long n = strtol(v.c_str(), &end, 10);
  if (v.length() == 0 || *end != '\0') return INT32_MIN;   // refused by the rule
  return (int)n;
}

void docRoutes(WebServer& srv) {
  srv.on("/api/touch", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    DocTouch t;
    const DocTouchAsk ask = {intArg(srv, "x", -1), intArg(srv, "y", -1), intArg(srv, "ms", 0),
                             intArg(srv, "x2", -1), intArg(srv, "y2", -1)};
    if (!docTouchMake(ask, t)) {
      srv.send(400, "application/json",
               "{\"error\":\"x 0-479, y 0-319, ms 20-3000, x2 and y2 together\"}");
      return;
    }
    docTouchStart(t);
    if (t.x2 == t.x && t.y2 == t.y) {
      logSDf("Acceptance build: web touch %d,%d for %u ms", t.x, t.y, (unsigned)t.ms);
    } else {
      logSDf("Acceptance build: web swipe %d,%d to %d,%d in %u ms", t.x, t.y, t.x2, t.y2, (unsigned)t.ms);
    }
    srv.send(200, "application/json", String("{\"queued\":true,\"ms\":") + t.ms + "}");
  });

  srv.on("/api/sim/weight", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    float g = 0.0f;
    bool off = false;
    if (!docSimParse(srv.arg("g").c_str(), g, off)) {
      srv.send(400, "application/json", "{\"error\":\"g from -100 to 10000, or off\"}");
      return;
    }
    if (off) {
      docSimOff();
      logSD("Acceptance build: simulated weight off");
      srv.send(200, "application/json", "{\"sim\":false}");
      return;
    }
    docSimSet(g);
    logSDf("Acceptance build: simulated weight %.1f g", g);
    srv.send(200, "application/json", String("{\"sim\":true,\"g\":") + String(g, 1) + "}");
  });

#if defined(BATTERY_DIAG)
  // Measurement of the 7B battery input (w05), reverted after.
  batteryDiagRoutes(srv);
#endif
}

#endif  // SSC_ACCEPTANCE
