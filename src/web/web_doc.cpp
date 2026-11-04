#include "web/web_doc.h"

#if defined(SSC_ACCEPTANCE)

#include <stdint.h>
#include <stdlib.h>

#include "app/doc_build.h"
#include "boards/board.h"
#include "hardware/sd_logger.h"
#include "services/battery.h"
#include "services/filament_db.h"
#include "services/sim_answer_rules.h"
#include "services/sim_tag_rules.h"
#include "services/tag_family.h"
#include "ui/tare_missing_popup.h"
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
//   POST /api/sim/tag-family?uid=<8 hex>&family=bambu|creality|snapmaker|forget|show
//                                           a made-up tag for the tag family
//                                           memory (v0.8.7-beta.1)
//   POST /api/sim/server-answer?code=<400-599>&count=<1-50>[&path=<word>][&retry=<s>] | code=off
//                                           the next requests to the scale's
//                                           server get that answer, unsent
//                                           (v0.8.7-beta.2)
//   POST /api/sim/tag?uid=<14 hex> | uid=off
//                                           a made-up empty NTAG215 on the
//                                           reader (v0.8.7-beta.2)
//   POST /api/sim/popup?name=tare-missing   a new popup opened for the picture
//                                           (v0.8.7-beta.2)
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

// A tag UID of four bytes from eight hex digits, false for anything else.
static bool uidArg(const String& v, uint8_t out[4]) {
  if (v.length() != 8) return false;
  for (int i = 0; i < 8; i++) {
    const char c = v[i];
    const int d = (c >= '0' && c <= '9') ? c - '0'
                : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    if (i % 2 == 0) out[i / 2] = (uint8_t)(d << 4);
    else            out[i / 2] |= (uint8_t)d;
  }
  return true;
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

  // The tag family memory told about a made-up tag (Spec 5.3 AA,
  // v0.8.7-beta.1). The memory is read only when a tag lies on the reader,
  // and the acceptance cannot place one; to show that it and the stored
  // index above keep apart (n69), a family is remembered here the way a
  // placement does, forgotten again (a tombstone, as tagFamilySet() writes
  // it), or the memory is loaded and shown. Only while Creality or Snapmaker
  // tags are on, as the scale's own placements.
  srv.on("/api/sim/tag-family", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    if (!tagFamilyInUse()) {
      srv.send(409, "application/json", "{\"error\":\"switch Creality or Snapmaker tags on first\"}");
      return;
    }
    const String fam = srv.arg("family");
    TagFamily set = TAG_FAMILY_UNKNOWN;
    bool show = false;
    bool known = true;
    if (fam == "bambu") set = TAG_FAMILY_BAMBU;
    else if (fam == "creality") set = TAG_FAMILY_CREALITY;
    else if (fam == "snapmaker") set = TAG_FAMILY_SNAPMAKER;
    else if (fam == "show") show = true;
    else known = (fam == "forget");
    uint8_t uid[4] = {0, 0, 0, 0};
    if (!known || !uidArg(srv.arg("uid"), uid)) {
      srv.send(400, "application/json",
               "{\"error\":\"uid as 8 hex digits, family bambu|creality|snapmaker|forget|show\"}");
      return;
    }
    if (!show) tagFamilySet(uid, set);
    const TagFamily now = tagFamilyGet(uid);
    const char now_c = now == TAG_FAMILY_UNKNOWN ? '-' : (char)now;
    char state[80];
    tagFamilyStatus(state, sizeof(state));
    logSDf("Acceptance build: tag memory %s %02X%02X%02X%02X, now %c; %s", fam.c_str(), uid[0], uid[1],
           uid[2], uid[3], now_c, state);
    srv.send(200, "application/json",
             String("{\"family\":\"") + now_c + "\",\"memory\":\"" + state + "\"}");
  });

  // A made-up answer of the scale's own server (Spec 5.3 AA, v0.8.7-beta.2):
  // the refusals of a server and a weight it does not take, which Jörg's
  // BamBuddy cannot be made to give without harm. BackendHttp gives the code
  // to the next `count` requests whose path holds `path` (any without it),
  // and sends nothing; a 429 carries `retry` as its Retry-After, a refusal is
  // recorded as a real one. code=off ends it before the count is used up.
  srv.on("/api/sim/server-answer", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    if (srv.arg("code") == "off") {
      docAnswerOff();
      logSD("Acceptance build: made-up server answer off");
      srv.send(200, "application/json", "{\"answer\":false}");
      return;
    }
    const int code  = intArg(srv, "code", -1);
    const int count = intArg(srv, "count", 1);
    const String word  = srv.arg("path");
    const String retry = srv.arg("retry");
    if (!simAnswerArgsOk(code, count, word.c_str(), retry.c_str())) {
      srv.send(400, "application/json",
               "{\"error\":\"code 400-599 or off, count 1-50, path up to 39 URL characters, retry digits\"}");
      return;
    }
    docAnswerSet(code, count, word.c_str(), retry.c_str());
    logSDf("Acceptance build: the next %d request(s) to the server%s%s get HTTP %d%s%s, unsent", count,
           word.length() ? " with " : "", word.c_str(), code, retry.length() ? ", Retry-After " : "",
           retry.c_str());
    srv.send(200, "application/json", String("{\"answer\":true,\"code\":") + code + ",\"count\":" + count + "}");
  });

  // A made-up, empty NTAG215 on the reader (Spec 5.3 AA, v0.8.7-beta.2): the
  // lookup of a tag nobody knows runs only once one lies there. The reader's
  // door answers with it until uid=off; it is never written, and a real tag
  // is not looked at meanwhile.
  srv.on("/api/sim/tag", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    const String uid_arg = srv.arg("uid");
    if (uid_arg == "off") {
      docTagOff();
      logSD("Acceptance build: made-up tag off the reader");
      srv.send(200, "application/json", "{\"tag\":false}");
      return;
    }
    uint8_t uid[SIM_TAG_UID_LEN];
    if (!simTagUidParse(uid_arg.c_str(), uid)) {
      srv.send(400, "application/json", "{\"error\":\"uid as 14 hex digits (7 bytes), or off\"}");
      return;
    }
    docTagSet(uid);
    logSDf("Acceptance build: made-up tag %s on the reader (empty NTAG215)", uid_arg.c_str());
    srv.send(200, "application/json", String("{\"tag\":true,\"uid\":\"") + uid_arg + "\"}");
  });

  // A new popup opened for the picture (Spec 5.3 AA, v0.8.7-beta.2): "Leergewicht
  // fehlt" comes only from servers that refuse a weight without an empty weight,
  // and Jörg's Spoolman never does. The rule behind it is the host test's.
  srv.on("/api/sim/popup", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    if (srv.arg("name") != "tare-missing") {
      srv.send(400, "application/json", "{\"error\":\"name=tare-missing\"}");
      return;
    }
    showTareMissingPopup();
    logSD("Acceptance build: popup tare-missing opened for the picture");
    srv.send(200, "application/json", "{\"popup\":\"tare-missing\"}");
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
