// Klipper: the printers the scale can put spools into, set up from the desk.
//
// The scale knows a Klipper printer by a name, an address, a port and
// perhaps an API key (services/klipper_store.h). This page lists them, adds,
// changes and deletes them, switches "storage location = printer name" and
// runs the connection test: a read-only walk through Moonraker that says in
// plain words what is missing (services/moonraker_api.h, moonrakerProbe).
//
// The test takes seconds, so it runs as a web job on a task of its own
// (web_jobs.h). Whoever starts it hands the job a copy of the printer and of
// its key, and the page asks the same URL again until the answer is ready;
// the handler that collects the answer writes it to the store, because the
// store belongs to the loop task (R3). Saving a printer starts the test by
// itself, on the scale and not from the page (R30): a printer whose test never
// ran cannot be used at the AMS view, so a page that is closed before it
// starts the test must not leave the printer in that state.
//
// A key never leaves the scale: the list only says whether there is one.
#include "web/web_pages.h"

#include <Arduino.h>
#include <WebServer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/klipper_printers.h"
#include "services/klipper_store.h"
#include "web/web_access.h"
#include "web/web_jobs.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char* label() { return T(STR_W_NAV_KLIPPER); }

// Klipper printers share the scale's Spoolman, so the page exists only where
// the scale has one (Spoolman directly, or Bambuddy keeping its spools there).
static bool applies() { return klipperAvailable(); }

// The routes are registered once, at boot, whatever the backend is; they
// answer only where the page exists.
static bool usable(WebServer &srv) {
  if (klipperAvailable()) return true;
  srv.send(404, "text/plain", T(STR_W_ERROR));
  return false;
}

// ---- what the last test found beyond what the store keeps -------------------
// The store keeps the verdict, the layout, the number of fields, the hints and
// the time. Three words of the texts are not in it: Klipper's state ("startup"),
// the code of an answer that was none, and the name of a system the scale cannot
// drive. They are remembered here, in RAM, for the few printers tested last;
// after a restart the list says the verdict without them.
#define KL_DETAIL_SLOTS 8

struct TestDetail {
  uint32_t id;                // 0: the slot is free
  int16_t  code;
  char     state[KL_STATE_MAX];
  char     unknown[KL_LANE_MAX];
};

static TestDetail s_detail[KL_DETAIL_SLOTS];
static uint8_t    s_detail_next = 0;

static const TestDetail* detailOf(uint32_t id) {
  if (id == 0) return nullptr;
  for (const TestDetail &d : s_detail)
    if (d.id == id) return &d;
  return nullptr;
}

static void detailPut(const WebJobResult &r) {
  TestDetail *slot = nullptr;
  for (TestDetail &d : s_detail)
    if (d.id == r.kl_id) slot = &d;
  if (!slot) {
    slot = &s_detail[s_detail_next];
    s_detail_next = (uint8_t)((s_detail_next + 1) % KL_DETAIL_SLOTS);
  }
  slot->id = r.kl_id;
  // A 200 that held no Klipper state is told as the 200 it was.
  slot->code = (r.code == KL_E_PARSE) ? 200 : (int16_t)r.code;
  snprintf(slot->state, sizeof(slot->state), "%s", r.kl_state);
  snprintf(slot->unknown, sizeof(slot->unknown), "%s", r.kl_unknown);
}

static void detailDrop(uint32_t id) {
  for (TestDetail &d : s_detail)
    if (d.id == id) d.id = 0;
}

// ---- the words ------------------------------------------------------------
// The stored test and layout are range checked here (R18): the record decodes
// any byte, a text table is indexed by nothing that was read from it, and the
// number of fields is a count to print, never a bound.

static int verdictString(uint8_t test) {
  switch (test) {
    case KT_OK:               return STR_W_KL_T_OK;
    case KT_UNREACHABLE:      return STR_W_KL_T_UNREACHABLE;
    case KT_ACCESS:           return STR_W_KL_T_ACCESS;
    case KT_KLIPPY:           return STR_W_KL_T_KLIPPY;
    case KT_NO_SPOOLMAN:      return STR_W_KL_T_NO_SPOOLMAN;
    case KT_SM_OFFLINE:       return STR_W_KL_T_SM_OFFLINE;
    case KT_OTHER_SPOOLMAN:   return STR_W_KL_T_OTHER;
    case KT_BAD_ANSWER:       return STR_W_KL_T_BAD_ANSWER;
    default:                  return STR_W_KL_T_NONE;   // never tested, or a value no firmware wrote
  }
}

// What the printer has, as the test found it; "" when the test read nothing.
static void layoutText(const KlipperTestOutcome &o, const TestDetail *d, char *out, size_t n) {
  out[0] = '\0';
  switch (o.layout) {
    case KL_LAYOUT_MMU:   snprintf(out, n, T(STR_W_KL_L_MMU), (int)o.fields); break;
    case KL_LAYOUT_AFC:   snprintf(out, n, T(STR_W_KL_L_AFC), (int)o.fields); break;
    case KL_LAYOUT_TOOLS: snprintf(out, n, T(STR_W_KL_L_TOOLS), (int)o.fields); break;
    case KL_LAYOUT_SINGLE:
      if (d && d->unknown[0]) snprintf(out, n, T(STR_W_KL_L_UNKNOWN), d->unknown);
      else                    snprintf(out, n, "%s", T(STR_W_KL_L_SINGLE));
      break;
    default: break;
  }
}

// The verdict of the stored test in words. A word the store does not keep comes
// from the detail of this boot, "?" or "-" where there is none.
static void verdictText(const KlipperPrinterCfg &c, char *out, size_t n) {
  const TestDetail *d = detailOf(c.id);
  char arg[112] = "";
  switch (c.last.test) {
    case KT_OK:
      layoutText(c.last, d, arg, sizeof(arg));
      if (!arg[0]) snprintf(arg, sizeof(arg), "?");
      break;
    case KT_KLIPPY:
      snprintf(arg, sizeof(arg), "%s", (d && d->state[0]) ? d->state : "?");
      break;
    case KT_OTHER_SPOOLMAN:
      snprintf(arg, sizeof(arg), "%s", klipperScaleSpoolman());
      break;
    case KT_BAD_ANSWER:
      if (d && d->code > 0) snprintf(arg, sizeof(arg), "%d", (int)d->code);
      else                  snprintf(arg, sizeof(arg), "-");
      break;
    default:
      break;
  }
  // A text without a conversion ignores the argument.
  snprintf(out, n, T(verdictString(c.last.test)), arg);
}

static void addNote(String &j, bool *any, int string_id) {
  if (*any) j += ',';
  *any = true;
  j += '"';
  j += jsonEsc(T(string_id));
  j += '"';
}

// The hints that come with what the printer has, as a JSON array of texts.
static void notesJson(String &j, const KlipperTestOutcome &o) {
  bool any = false;
  j += '[';
  if (o.flags & KL_FLAG_TOOLS_WITHOUT_VAR) addNote(j, &any, STR_W_KL_N_NOVAR);
  if (o.flags & KL_FLAG_TRUNCATED)         addNote(j, &any, STR_W_KL_N_TRUNC);
  j += ']';
}

// ---- keys -----------------------------------------------------------------
// The list says whether there is a key and nothing more. The store hands a key
// out through a buffer, so the buffer is only looked at for emptiness.
static bool keyIsSet(uint32_t id) {
  char probe[KL_KEY_MAX] = "";
  return klipperStoreKey(id, probe, sizeof(probe)) && probe[0] != '\0';
}

// R3: what the test job gets is a copy of the printer and of its key, made here
// on the loop task while no job runs. The copy of the key is wiped when it has
// been handed over.
static bool handOver(const KlipperPrinterCfg &c) {
  char secret[KL_KEY_MAX] = "";
  klipperStoreKey(c.id, secret, sizeof(secret));   // "" when the printer has none
  const bool ok = webJobSetKlipperInput(c, secret);
  volatile char *wipe = secret;
  for (size_t i = 0; i < sizeof(secret); i++) wipe[i] = 0;
  return ok;
}

// ---- what was typed -------------------------------------------------------
// Digits only; empty or 0 is Moonraker's own port (the store makes it 7125).
static bool parsePort(const char *s, uint16_t *out) {
  uint32_t v = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
    v = v * 10 + (uint32_t)(*s - '0');
    if (v > 65535) return false;
  }
  *out = (uint16_t)v;
  return true;
}

// The name and the address cleaned by the store's own rules (a text too long
// for its field is refused, not cut in the middle of a word), the port as a
// number. The store judges the rest.
static KlipperSave readPrinter(WebServer &srv, KlipperPrinterCfg *c) {
  if (!klipperNameClean(srv.arg("name").c_str(), c->name, sizeof(c->name))) return KLS_BAD_NAME;
  if (!klipperHostClean(srv.arg("host").c_str(), c->host, sizeof(c->host))) return KLS_BAD_HOST;
  String port = srv.arg("port");
  port.trim();
  return parsePort(port.c_str(), &c->port) ? KLS_OK : KLS_BAD_PORT;
}

static void saveRefused(WebServer &srv, KlipperSave r) {
  int status = 400;
  int text = STR_W_ERROR;
  switch (r) {
    case KLS_BAD_NAME:     text = STR_W_KL_E_NAME; break;
    case KLS_NAME_TAKEN:   status = 409; text = STR_W_KL_E_TAKEN; break;
    case KLS_BAD_HOST:     text = STR_W_KL_E_HOST; break;
    case KLS_BAD_PORT:     text = STR_W_KL_E_PORT; break;
    case KLS_BAD_KEY:      text = STR_W_KL_E_KEY; break;
    case KLS_FULL:         status = 409; text = STR_W_KL_FULL; break;   // printers or keys, both "no room"
    case KLS_NOT_FOUND:    status = 404; text = STR_W_KL_E_GONE; break;
    case KLS_WRITE_FAILED: status = 500; text = STR_W_KL_E_WRITE; break;
    default: break;
  }
  logSDf("Web: Klipper printer not saved (%d)", (int)r);
  srv.send(status, "text/plain", T(text));
}

// ---- the list ---------------------------------------------------------------
// One printer as a piece of the JSON list. The list goes out piece by piece:
// 64 printers with their texts would be a block of 20 kB of the internal heap.
static void sendPrinter(WebServer &srv, const KlipperPrinterCfg &c, bool first) {
  char text[256];
  verdictText(c, text, sizeof(text));
  String j;
  j.reserve(640);
  if (!first) j += ',';
  j += F("{\"id\":");
  j += String((unsigned long)c.id);
  j += F(",\"name\":\"");
  j += jsonEsc(c.name);
  j += F("\",\"host\":\"");
  j += jsonEsc(c.host);
  j += F("\",\"port\":");
  j += String((unsigned)c.port);
  j += F(",\"key\":");
  j += keyIsSet(c.id) ? F("true") : F("false");
  j += F(",\"layout\":");
  j += String((unsigned)c.last.layout);
  j += F(",\"fields\":");
  j += String((unsigned)c.last.fields);
  j += F(",\"test\":");
  j += String((unsigned)c.last.test);
  j += F(",\"test_text\":\"");
  j += jsonEsc(text);
  j += F("\",\"notes\":");
  notesJson(j, c.last);
  j += F(",\"tested_at\":");
  j += String((unsigned long)c.last.at);
  j += '}';
  srv.sendContent(j);
}

// ---- the page ---------------------------------------------------------------
static void listCard(String &h) {
  h += F("<div class='card wide'><h2>");
  h += T(STR_W_KL_C_PRINTERS);
  h += F("</h2><p class='hint'>");
  h += T(STR_W_KL_INTRO);
  h += F("</p><div id='kl-list' style='margin-top:14px'><p class='hint'>");
  h += T(STR_W_LOADING);
  h += F("</p></div><p class='hint' id='kl-room' style='margin-top:12px'></p>"
         "<span class='msg' id='kl-s'></span></div>");
}

// One form for adding and changing: the title and the cancel button follow
// which of the two it is. The port starts at Moonraker's own, the key field
// stays empty (empty means unchanged), and the key can be taken away with the
// box, which is only there for a printer that has one.
static void formCard(String &h) {
  h += F("<div class='card wide'><h2 id='kl-ft'>");
  h += T(STR_W_KL_ADD);
  h += F("</h2><div class='field'><label for='kn'>");
  h += T(STR_W_R_NAME);
  h += F("</label><div class='inrow'><input id='kn' type='text' spellcheck='false'></div>"
         "<span class='hint'>");
  h += T(STR_W_KL_NAME_HINT);
  h += F("</span></div><div class='field'><label for='kh'>");
  h += T(STR_W_R_ADDRESS);
  h += F("</label><div class='inrow'><input id='kh' type='text' maxlength='63' spellcheck='false' "
         "placeholder='voron.local'></div><span class='hint'>");
  h += T(STR_W_KL_HOST_HINT);
  h += F("</span></div><div class='field'><label for='kp'>");
  h += T(STR_W_KL_PORT);
  h += F("</label><div class='inrow'><input id='kp' type='number' min='0' max='65535' value='7125'>"
         "</div></div><div class='field'><label for='kk'>");
  h += T(STR_W_APIKEY);
  h += F("</label><div class='inrow'><input id='kk' type='password' maxlength='47' "
         "autocomplete='new-password' spellcheck='false'></div><span class='hint'>");
  h += T(STR_W_KL_KEY_HINT);
  h += F("</span><label class='check' id='kd-l' style='display:none'><span class='switch'>"
         "<input id='kd' type='checkbox'><i></i></span>");
  h += T(STR_W_KL_KEY_DEL);
  h += F("</label></div><div class='inrow' style='margin-top:16px'><button id='kl-save' type='button'>");
  h += T(STR_W_SAVE);
  h += F("</button><button id='kl-cancel' class='quiet' type='button' style='display:none'>");
  h += T(STR_CANCEL);
  h += F("</button><span class='msg' id='kl-fs'></span></div></div>");
}

static void locCard(String &h) {
  h += F("<div class='card wide'><label class='check'><span class='switch'><input id='kl-loc' type='checkbox'");
  if (klipperLocSwitch()) h += F(" checked");
  h += F("><i></i></span>");
  h += T(STR_W_KL_LOC);
  h += F("</label><span class='msg' id='kl-loc-s'></span><span class='hint'>");
  h += T(STR_W_KL_LOC_HINT);
  h += F("</span></div>");
}

// The texts the script writes, in the page's language. The block opens here and
// closes in scriptHandlers(): the helpers of the script are defined in the order
// they emit, so that scripts/check_web_js.py, which reads the file in text order,
// finds the whole script between the two tags.
static void scriptStrings(String &h) {
  h += F("<script>");
  h += webShellJsStrings();
  h += F("const K={none:");
  h += jsStr(T(STR_W_KL_NONE));
  h += F(",room:");
  h += jsStr(T(STR_W_KL_ROOM));
  h += F(",full:");
  h += jsStr(T(STR_W_KL_FULL));
  h += F(",add:");
  h += jsStr(T(STR_W_KL_ADD));
  h += F(",edit:");
  h += jsStr(T(STR_W_KL_EDIT));
  h += F(",change:");
  h += jsStr(T(STR_W_KL_CHANGE));
  h += F(",del:");
  h += jsStr(T(STR_W_LOG_DELETE));
  h += F(",delAsk:");
  h += jsStr(T(STR_W_KL_DELETE_ASK));
  h += F(",test:");
  h += jsStr(T(STR_W_KL_TEST));
  h += F(",testing:");
  h += jsStr(T(STR_W_KL_TESTING));
  h += F(",keySet:");
  h += jsStr(T(STR_W_KL_KEY_SET));
  h += F(",tested:");
  h += jsStr(T(STR_W_KL_TESTED));
  h += F(",busy:");
  h += jsStr(T(STR_W_KL_BUSY));
  h += F("};");
}

// Named arguments in a form-encoded body: the key travels in the body and
// never in an address. Answers like post() of /app.js, which sends plain text.
static void scriptPost(String &h) {
  h += F("function kpost(u,o){return fetch(u,{method:'POST',"
         "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
         "body:new URLSearchParams(o).toString()}).then(function(r){"
         "return r.text().then(function(t){let j=null;try{j=JSON.parse(t);}catch(e){}"
         "return{ok:r.ok,text:t,json:j};});})"
         ".catch(function(){return{ok:false,text:'',json:null};});}");
}

// The list on the page. Every text of a printer goes in with textContent: a
// name is whatever was typed.
static void scriptList(String &h) {
  h += F("let EDIT=0;"
         "function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;"
         "if(x!==undefined)e.textContent=x;return e;}"
         "function btn(t,c,f){const b=el('button',c,t);b.type='button';"
         "b.addEventListener('click',f);return b;}"
         // The verdict: 1 is "all well", 0 "never tested", anything else a fault.
         "function row(p){const r=el('div','listrow'),l=el('div'),a=el('span');"
         "l.style.cssText='display:flex;flex-direction:column;gap:3px;min-width:0';"
         "l.appendChild(el('b','',p.name));"
         "l.appendChild(el('span','sz',p.host+':'+p.port+(p.key?'  \\u00b7  '+K.keySet:'')));"
         "l.appendChild(el('span',p.test===1?'msg':p.test===0?'hint':'msg bad',p.test_text));"
         "p.notes.forEach(function(n){l.appendChild(el('span','hint',n));});"
         "if(p.tested_at)l.appendChild(el('span','hint',"
         "K.tested.replace('%s',new Date(p.tested_at*1000).toLocaleString())));"
         "a.style.cssText='display:flex;gap:8px;flex-wrap:wrap';"
         "a.appendChild(btn(K.change,'quiet',function(){editPrinter(p);}));"
         "a.appendChild(btn(K.test,'quiet',function(){testNow(p.id);}));"
         "a.appendChild(btn(K.del,'danger',function(){delPrinter(p);}));"
         "r.appendChild(l);r.appendChild(a);return r;}"
         "function paint(d){const box=$('kl-list');box.textContent='';"
         "if(!d.printers.length)box.appendChild(el('p','hint',K.none));"
         "d.printers.forEach(function(p){box.appendChild(row(p));});"
         "$('kl-room').textContent=d.room>0?K.room.replace('%d',d.room):K.full;"
         "$('kl-loc').checked=d.loc;}"
         "function load(){getJson('/api/klipper').then(function(d){"
         "if(!d){flash('kl-s',WS.err,true);return;}paint(d);});}");
}

// The form: one for adding and changing. The key goes out only when something
// was typed (keyset), and is taken away only with the box (keydel).
static void scriptForm(String &h) {
  h += F("function resetForm(){EDIT=0;$('kn').value='';$('kh').value='';$('kp').value='7125';"
         "$('kk').value='';$('kk').placeholder='';$('kd').checked=false;"
         "$('kd-l').style.display='none';$('kl-ft').textContent=K.add;"
         "$('kl-cancel').style.display='none';}"
         "function editPrinter(p){EDIT=p.id;$('kn').value=p.name;$('kh').value=p.host;"
         "$('kp').value=p.port;$('kk').value='';$('kk').placeholder=p.key?K.keySet:'';"
         "$('kd').checked=false;$('kd-l').style.display=p.key?'':'none';"
         "$('kl-ft').textContent=K.edit;$('kl-cancel').style.display='';"
         "$('kl-ft').scrollIntoView({behavior:'smooth'});$('kn').focus();}"
         "function saveForm(){const key=$('kk').value.trim();"
         "const o={id:EDIT,name:$('kn').value,host:$('kh').value,port:$('kp').value,"
         "key:key,keyset:key?1:0,keydel:$('kd').checked?1:0};"
         "$('kl-save').disabled=true;"
         // The scale started the test with the save; when it could not (another
         // job was running) the page asks for it, and either way waits for it.
         // The callback is one literal: test_klipper_page.py reads the script
         // literal by literal and keeps those that show JavaScript.
         "kpost('/api/klipper/save',o).then(function(r){$('kl-save').disabled=false;if(!r.ok){flash('kl-fs',r.text||WS.err,true);return;}flash('kl-fs',WS.ok,false,3000);resetForm();if(r.json&&r.json.testing)pollTest(0);else if(r.json)testNow(r.json.id);else load();});}");
}

// The test: started by the scale after a save, or by the button; the page asks
// the same address again until the answer is in and then reads the list.
static void scriptTest(String &h) {
  h += F("function pollTest(n){getJson('/api/klipper/test').then(function(d){"
         "if(d&&d.state==='running'&&n<60){flash('kl-s',K.testing,false);"
         "setTimeout(function(){pollTest(n+1);},700);return;}"
         "flash('kl-s','',false);load();});}"
         "function testNow(id){flash('kl-s',K.testing,false);"
         "kpost('/api/klipper/test',{id:id}).then(function(r){"
         "if(!r.ok||!r.json){flash('kl-s',r.text||WS.err,true,5000);return;}"
         "if(r.json.queued===false){flash('kl-s',K.busy,true,5000);return;}"
         "pollTest(0);});}"
         "function delPrinter(p){"
         "ask(K.delAsk.replace('%s',function(){return p.name;}),K.del,true).then(function(ok){"
         "if(!ok)return;kpost('/api/klipper/delete',{id:p.id}).then(function(r){"
         "flash('kl-s',r.ok?WS.ok:(r.text||WS.err),!r.ok,4000);"
         "if(EDIT===p.id)resetForm();load();});});}");
}

// Every handler is bound here rather than written into an attribute, and the
// page asks for the test state first: a test that was started before the page
// was opened is waited for, and one that finished is collected.
static void scriptHandlers(String &h) {
  h += F("$('kl-save').addEventListener('click',saveForm);"
         "$('kl-cancel').addEventListener('click',resetForm);"
         "['kn','kh','kp','kk'].forEach(function(i){$(i).addEventListener('keydown',function(e){"
         "if(e.key==='Enter')saveForm();});});"
         "$('kl-loc').addEventListener('change',function(){const on=$('kl-loc').checked;"
         "kpost('/api/klipper/loc',{on:on?1:0}).then(function(r){"
         "flash('kl-loc-s',r.ok?WS.ok:(r.text||WS.err),!r.ok,3000);"
         "if(!r.ok)$('kl-loc').checked=!on;});});"
         "pollTest(0);"
         "</script>");
}

// The script, in the order the helpers above are written in; the tags that
// open and close it are theirs.
static void pageScript(String &h) {
  scriptStrings(h);
  scriptPost(h);
  scriptList(h);
  scriptForm(h);
  scriptTest(h);
  scriptHandlers(h);
}

static String body() {
  String h;
  h.reserve(11000);
  h += F("<div class='grid'>");
  listCard(h);
  formCard(h);
  locCard(h);
  h += F("</div>");
  pageScript(h);
  return h;
}

static void routes(WebServer &srv) {
  // The list, a piece for each printer. Never a key: "key" says whether there
  // is one.
  srv.on("/api/klipper", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_KLIPPER))) return;
    if (!usable(srv)) return;
    srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv.send(200, "application/json", "");
    String head = F("{\"loc\":");
    head += klipperLocSwitch() ? F("true") : F("false");
    head += F(",\"room\":");
    head += String(klipperStoreRoomLeft());
    head += F(",\"printers\":[");
    srv.sendContent(head);
    const int count = klipperStoreCount();
    bool first = true;
    for (int i = 0; i < count; i++) {
      KlipperPrinterCfg c;
      if (!klipperStoreAt(i, &c)) continue;
      sendPrinter(srv, c, first);
      first = false;
    }
    srv.sendContent("]}");
    srv.sendContent("");
  });

  // Adds a printer (id 0) or changes one. The key: keyset=1 puts the one in
  // "key" in, keydel=1 takes the stored one away, neither leaves it. A saved
  // printer is tested at once, here on the scale (R30). The answer is the
  // number of the printer and whether the test started; a refusal is the
  // reason as plain text.
  srv.on("/api/klipper/save", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_KLIPPER))) return;
    if (!usable(srv)) return;
    KlipperPrinterCfg c;
    memset(&c, 0, sizeof(c));
    c.id = (uint32_t)strtoul(srv.arg("id").c_str(), nullptr, 10);
    const KlipperSave typed = readPrinter(srv, &c);
    if (typed != KLS_OK) { saveRefused(srv, typed); return; }
    String key = srv.arg("key");
    key.trim();
    const char *key_arg = nullptr;                 // the stored key stays
    if (srv.arg("keydel") == "1") key_arg = "";    // taken away
    else if (srv.arg("keyset") == "1" && key.length()) key_arg = key.c_str();
    const KlipperSave res = klipperStoreSave(&c, key_arg);
    if (res != KLS_OK) { saveRefused(srv, res); return; }

    // The same start as POST /api/klipper/test.
    bool testing = false;
    if (webJobState() == WJS_DONE) webJobTake();   // somebody else's leftover
    if (handOver(c)) testing = webJobStart(WJ_KLIPPER_TEST, klipperScaleSpoolman(), false);
    logSDf("Web: Klipper printer %lu saved, key %s, test %s", (unsigned long)c.id,
           key_arg ? (key_arg[0] ? "set" : "removed") : "unchanged",
           testing ? "started" : "not started");
    String j = F("{\"ok\":true,\"id\":");
    j += String((unsigned long)c.id);
    j += F(",\"testing\":");
    j += testing ? F("true") : F("false");
    j += '}';
    srv.send(200, "application/json", j);
  });

  srv.on("/api/klipper/delete", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_KLIPPER))) return;
    if (!usable(srv)) return;
    const uint32_t id = (uint32_t)strtoul(srv.arg("id").c_str(), nullptr, 10);
    KlipperPrinterCfg c;
    if (!klipperStoreFind(id, &c)) { srv.send(404, "text/plain", T(STR_W_KL_E_GONE)); return; }
    if (!klipperStoreDelete(id)) { srv.send(500, "text/plain", T(STR_W_KL_E_WRITE)); return; }
    detailDrop(id);
    logSDf("Web: Klipper printer %lu deleted", (unsigned long)id);
    srv.send(200, "application/json", "{\"ok\":true}");
  });

  // POST starts the connection test of a printer and answers 202; the same
  // address answers the state and, once it is done, collects the result. The
  // job ran on a task of its own and only filled in the answer: the store is
  // written here, on the loop task (R3).
  srv.on("/api/klipper/test", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_KLIPPER))) return;
    if (!usable(srv)) return;
    const uint32_t id = (uint32_t)strtoul(srv.arg("id").c_str(), nullptr, 10);
    KlipperPrinterCfg c;
    if (!klipperStoreFind(id, &c)) { srv.send(404, "text/plain", T(STR_W_KL_E_GONE)); return; }
    if (webJobState() == WJS_DONE) webJobTake();   // somebody else's leftover
    if (!handOver(c) || !webJobStart(WJ_KLIPPER_TEST, klipperScaleSpoolman(), false)) {
      srv.send(200, "application/json", "{\"queued\":false}");
      return;
    }
    logSDf("Web: Klipper test of printer %lu started", (unsigned long)id);
    srv.send(202, "application/json", "{\"queued\":true}");
  });
  srv.on("/api/klipper/test", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_KLIPPER))) return;
    if (!usable(srv)) return;
    if (webJobState() == WJS_RUNNING && webJobKind() == WJ_KLIPPER_TEST) {
      srv.send(202, "application/json", "{\"state\":\"running\"}");
      return;
    }
    if (webJobState() == WJS_DONE && webJobKind() == WJ_KLIPPER_TEST) {
      const WebJobResult &r = webJobResult();
      const bool stored = klipperStoreSetTest(r.kl_id, r.kl);
      if (stored) detailPut(r);
      logSDf("Web: Klipper test of printer %lu: verdict %d, %s", (unsigned long)r.kl_id,
             (int)r.kl.test, stored ? "stored" : "not stored");
      String j = F("{\"state\":\"done\",\"id\":");
      j += String((unsigned long)r.kl_id);
      j += F(",\"stored\":");
      j += stored ? F("true") : F("false");
      j += '}';
      webJobTake();
      srv.send(200, "application/json", j);
      return;
    }
    srv.send(200, "application/json", "{\"state\":\"idle\"}");
  });

  // "on" is 1 or 0: state, not a toggle, like the other switches.
  srv.on("/api/klipper/loc", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_KLIPPER))) return;
    if (!usable(srv)) return;
    const bool on = srv.arg("on") == "1";
    klipperSetLocSwitch(on);
    logSDf("Web: Klipper location = printer name %s", on ? "on" : "off");
    srv.send(200, "application/json", on ? "{\"ok\":true,\"loc\":true}" : "{\"ok\":true,\"loc\":false}");
  });
}

extern const WebPage PAGE_KLIPPER;
const WebPage PAGE_KLIPPER = {
  "/klipper", label, GATE_CONFIG, applies,
  body, routes
};
