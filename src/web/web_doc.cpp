#include "web/web_doc.h"

#if defined(SSC_ACCEPTANCE)

#include <stdint.h>
#include <stdlib.h>

#include "app/doc_build.h"
#include "boards/board.h"
#include "hardware/sd_logger.h"
#include "services/battery.h"
#include "services/filament_db.h"
#include "web/web_access.h"
#include "lang.h"

// The routes of the acceptance and documentation builds (SSC_ACCEPTANCE, never
// in a release), behind the maintenance gate like the screenshots they serve:
//   POST /api/touch?x=&y=[&ms=][&x2=&y2=]  touch the display (480x320 coordinates)
//   POST /api/sim/weight?g=<grams>|off      a simulated weight on the pad,
//                                           which ends by itself after 15 minutes
//   POST /api/sim/battery?supply=battery|charging|none&mv=<mV> | supply=off
//                                           a simulated battery (7B, Spec 5.3 AA,
//                                           V60) through the battery rules;
//                                           &boot=1[&secs=] for the next start (V66)
//   POST /api/sim/fdb-forget                the next "Neu aus Datenbank" passes
//                                           the stored index over once (V64)
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

  // The stored index of "Neu aus Datenbank" passed over once (Spec 5.3 AA,
  // V64): the next opening reads the whole database and stores it again, so
  // the store step can be shown without waiting for the week to end. The
  // index in memory goes as well, or the picker would not load at all.
  srv.on("/api/sim/fdb-forget", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    if (!fdbForgetStoredIndexOnce()) {
      srv.send(409, "application/json", "{\"error\":\"a database load is running\"}");
      return;
    }
    logSD("Acceptance build: stored database index passed over once");
    srv.send(200, "application/json", "{\"forget\":true}");
  });

  // The battery as the rules would see it (Spec 5.3 AA, V60): readings made
  // up around mv with the spread the board showed for that supply. The
  // display, the warnings and the update lock follow within about twenty
  // seconds, as they would for a real one. "off" reads the chip again.
#if BOARD_BATTERY_SENSE
  srv.on("/api/sim/battery", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    const String supply = srv.arg("supply");
    const int mv = intArg(srv, "mv", 0);
    BatterySim mode = BAT_SIM_OFF;
    bool known = true;
    if (supply == "battery") mode = BAT_SIM_BATTERY;
    else if (supply == "charging") mode = BAT_SIM_CHARGING;
    else if (supply == "none") mode = BAT_SIM_NONE;
    else known = (supply == "off");
    const bool needs_mv = (mode == BAT_SIM_BATTERY || mode == BAT_SIM_CHARGING);
    if (!known || (needs_mv && (mv < 2500 || mv > 4400))) {
      srv.send(400, "application/json",
               "{\"error\":\"supply battery|charging with mv 2500-4400, none, or off\"}");
      return;
    }
    // boot=1: the reading of the next start instead, once, for secs seconds
    // of its wait (Spec V66); the running scale is left as it is.
    if (srv.arg("boot") == "1") {
      const int secs = intArg(srv, "secs", 30);
      if (mode != BAT_SIM_BATTERY || secs < 5 || secs > 120) {
        srv.send(400, "application/json", "{\"error\":\"boot=1 with supply=battery and secs 5-120\"}");
        return;
      }
      batterySimForStart((uint32_t)mv, (uint32_t)secs);
      logSDf("Acceptance build: the next start reads %d mV for %d s", mv, secs);
      srv.send(200, "application/json",
               String("{\"sim\":\"battery\",\"mv\":") + mv + ",\"boot\":true,\"secs\":" + secs + "}");
      return;
    }
    batterySimSet(mode, (uint32_t)mv);
    logSDf("Acceptance build: simulated battery %s, %d mV", supply.c_str(), mv);
    srv.send(200, "application/json", String("{\"sim\":\"") + supply + "\",\"mv\":" + mv + "}");
  });
#endif
}

#endif  // SSC_ACCEPTANCE
