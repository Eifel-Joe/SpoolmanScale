// The scale as the converter sees it: which chip answered, the raw count, the
// weight, how far the readings scatter. Built for the first hour after
// soldering, when the question is "does it react at all, and the right way
// round", and for the day the numbers look wrong.
//
// The raw count is read by the handler itself. That does not race with the
// weighing: the web server is served from appLoop(), the same task that reads
// the converter for the weight.
#include "web/web_pages.h"

#include <Arduino.h>
#include <WebServer.h>

#include "app/app_state.h"
#include "app_config.h"
#include "hardware/scale.h"
#include "services/user_options.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char* label() { return T(STR_W_NAV_SCALE); }

// A device built without a load cell has no scale to look at: no tab, 404.
static bool applies() { return g_scale_fitted; }

static void row(String &h, const char *id, int label_id) {
  h += F("<tr><td>");
  h += T(label_id);
  h += F("</td><td id='");
  h += id;
  h += F("' style='color:var(--ink);font-variant-numeric:tabular-nums'>-</td></tr>");
}

static String body() {
  String h;
  h.reserve(2400);
  h += F("<div class='grid'><div class='card wide'><h2>");
  h += T(STR_W_C_SCALE_LIVE);
  h += F("</h2><p class='hint'>");
  h += T(STR_W_SCALE_LIVE_HINT);
  h += F("</p><table>");
  row(h, "sl-adc", STR_W_SCALE_ADC);
  row(h, "sl-raw", STR_W_SCALE_RAW);
  row(h, "sl-g", STR_W_SCALE_WEIGHT);
  row(h, "sl-pp", STR_W_SCALE_SPREAD);
  row(h, "sl-cal", STR_W_SCALE_CAL);
  row(h, "sl-zero", STR_W_SCALE_ZERO);
  h += F("</table></div></div>"
         "<script>");
  h += webShellJsStrings();
  h += F("const SL={none:");
  h += jsStr(T(STR_W_SCALE_NONE));
  h += F("};"
         "function slSet(id,v){const e=$(id);if(e)e.textContent=v;}"
         "function slTick(){"
         "if(document.hidden)return;"
         "fetch('/api/scale/live',{cache:'no-store'}).then(r=>r.json()).then(d=>{"
         "slSet('sl-adc',d.ready?d.adc:SL.none);"
         "slSet('sl-raw',d.ready?d.raw:'-');"
         "slSet('sl-g',d.ready?d.g.toFixed(1)+' g':'-');"
         "slSet('sl-pp',d.full?d.spread.toFixed(2)+' g':'-');"
         "slSet('sl-cal',d.cal.toFixed(4));"
         "slSet('sl-zero',d.zero);}).catch(()=>{});}"
         "slTick();setInterval(slTick,500);"
         "</script>");
  return h;
}

static void routes(WebServer &srv) {
  srv.on("/api/scale/live", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SCALE))) return;
    float lo = scale_filter_buf[0], hi = scale_filter_buf[0];
    for (int i = 1; i < SCALE_FILTER_SIZE; i++) {
      if (scale_filter_buf[i] < lo) lo = scale_filter_buf[i];
      if (scale_filter_buf[i] > hi) hi = scale_filter_buf[i];
    }
    String j = F("{\"ready\":");
    j += scale_ready ? F("true") : F("false");
    j += F(",\"adc\":\"");
    j += scaleHardwareName();
    j += F("\",\"raw\":");
    j += scale_ready ? String(scaleHardwareReadRaw()) : String(0);
    j += F(",\"g\":");
    j += String(scale_weight_g, 2);
    j += F(",\"full\":");
    j += scale_filter_full ? F("true") : F("false");
    j += F(",\"spread\":");
    j += String(hi - lo, 3);
    j += F(",\"cal\":");
    j += String(cal_factor, 4);
    j += F(",\"zero\":");
    j += String(zero_offset);
    j += F("}");
    srv.send(200, "application/json", j);
  });
}

extern const WebPage PAGE_SCALE;
const WebPage PAGE_SCALE = {
  "/scale", label, GATE_CONFIG, applies,
  body, routes
};
