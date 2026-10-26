#include "web/web_printer_host.h"

#include "hardware/sd_logger.h"
#include "services/brother_ql_rules.h"
#include "web/web_access.h"
#include "web/web_printer_cards.h"
// Last on purpose: T() is a macro, see the pages.
#include "lang.h"

// Four numbers with dots: 255.255.255.255 is the longest.
#define HOST_INPUT_MAX 15

// The tile in the device tile's shape, so the two links read alike; its lines
// come from the state, like the device tile's. The field below it is where
// the address is typed and saved.
void webPrinterHostTile(String& h, const LabelPrinterConfig& c) {
  h += F("<div class='dev");
  if (!labelPrinterConfigured(c)) h += F(" empty");
  h += F("' id='ht'><div class='ic'>");
  webPrinterIcon(h);
  h += F("</div><div class='tx'><span class='nm' id='hn'></span><span class='sub' id='hd'></span>"
         "</div></div><div class='field'><label for='hi'>");
  h += T(STR_W_P_HOST);
  // A plain text field: inputmode='decimal' gives a German or French phone
  // a comma and no dot, and an address could not be typed at all.
  h += F("</label><div class='inrow'><input id='hi' type='text' "
         "autocomplete='off' spellcheck='false' maxlength='");
  h += String(HOST_INPUT_MAX);
  h += F("' placeholder='192.168.1.50'><button id='hs'>");
  h += T(STR_W_SAVE);
  h += F("</button></div><span class='msg' id='hs-s'></span><span class='hint'>");
  // Once on the printer: in "P-touch Template" a new QL prints nothing
  // (befunde 4).
  h += T(STR_W_P_HOST_HINT);
  h += F("</span></div>");
}

void webPrinterHostScript(String& h) {
  h += F(// The device tile's two lines: the model from its card over the
         // address, or "no printer" over the model. The field is left alone
         // while it is being typed in.
         "function host(d){const t=$('ht');if(!t)return;"
         "const on=!!d.host,m=document.querySelector('#pm .pc.on'),"
         "n=m?m.querySelector('.br').textContent+' '+m.querySelector('.mn').textContent:'';"
         "t.className='dev'+(on?'':' empty');"
         "$('hn').textContent=on?n:P.none;$('hd').textContent=on?d.host:n;"
         "const i=$('hi');if(document.activeElement!==i)i.value=d.host;}"
         // A refused address stays in the field to be corrected; only a saved
         // one brings the new state, with the print buttons it unlocks.
         "function saveHost(){postFlash('/api/printer/host',$('hi').value.trim(),'hs-s',6000)"
         ".then(function(r){if(r.ok)load();});}"
         "if($('hs')){$('hs').addEventListener('click',saveHost);"
         "$('hi').addEventListener('keydown',function(e){if(e.key==='Enter')saveHost();});}");
}

void webPrinterHostRoutes(WebServer& srv) {
  // Checked here as the save checks it (brotherQlHostValid), so a typo is
  // told apart from a write that failed: 400 with the rule, 500 for the store.
  // Nothing is the Brother's to touch but the address: the label is its
  // table's, the offset has its own route.
  srv.on("/api/printer/host", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_PRINTER))) return;
    String v = srv.arg("plain");
    v.trim();
    if (!v.isEmpty() && !brotherQlHostValid(v.c_str())) {
      logSD("Web: printer host -> refused, no IPv4 address");
      srv.send(400, "text/plain", T(STR_W_P_HOST_BAD));
      return;
    }
    const bool ok = labelPrinterSetHost(v.c_str());
    logSDf("Web: printer host -> %s %s", v.isEmpty() ? "none" : v.c_str(),
           ok ? "saved" : "NOT saved");
    srv.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
}

// The print card says "Sent to the printer" over the hint; the line has
// room for both, the scale's row only for the title.
String webPrinterResultLine(int result) {
  if (result != STR_PRN_NET_SENT_MSG) return String(T(result));
  String s = T(STR_PRN_NET_SENT_TITLE);
  s += F(". ");
  s += T(STR_PRN_NET_SENT_MSG);
  return s;
}
