#include "web/web_screenshot.h"

#include "app/screenshot.h"
#include "app_config.h"
#include "web/web_access.h"
#include "web/web_shell.h"

// Last: T() is a macro, and ArduinoJson uses T as a template parameter.
#include "lang.h"

static constexpr uint32_t MS_PER_S = 1000;
// Between two downloads of "save all": browsers drop clicks that come in one
// burst, and ask once whether the page may save several files.
static constexpr int SAVE_ALL_GAP_MS = 400;
// How long the copy button shows its tick before it is a copy button again.
static constexpr int COPIED_SHOWN_MS = 2000;

static void cardHtml(String& h) {
  // Icon buttons: a 38 px square, the height of every other button here, and
  // icon plus text on the two that act on all pictures.
  h += F("<style>.shi{width:38px;height:38px;padding:0;flex:none}"
         ".shi,.shw{display:inline-flex;align-items:center;justify-content:center;gap:6px}"
         ".shi svg,.shw svg{width:17px;height:17px;flex:none}"
         ".shi.ok{color:var(--good)}</style>");
  h += F("<div class='card wide'><h2>");
  h += T(STR_W_C_SHOT);
  h += F("</h2><p class='note'>");
  h += T(STR_W_SHOT_NOTE);
  h += F("</p><div style='display:flex;gap:12px;align-items:center;flex-wrap:wrap;margin:12px 0'>"
         "<button id='shb'>");
  h += T(STR_W_SHOT_TAKE);
  h += F("</button><button id='sha' class='quiet shw' disabled>");
  h += T(STR_W_SHOT_SAVE_ALL);
  h += F("</button><button id='shx' class='danger shw' disabled>");
  h += T(STR_W_SHOT_DROP_ALL);
  h += F("</button><span id='sht' class='note' style='margin:0'></span></div>"
         "<div id='shl' style='display:grid;gap:16px;"
         "grid-template-columns:repeat(auto-fill,minmax(240px,1fr))'></div></div>");
}

static void cardStrings(String& h) {
  h += F("<script>const SH={take:");
  h += jsStr(T(STR_W_SHOT_TAKE));
  h += F(",busy:");  h += jsStr(T(STR_W_SHOT_BUSY));
  h += F(",web:");   h += jsStr(T(STR_W_SHOT_AT_WEB));
  h += F(",panel:"); h += jsStr(T(STR_W_SHOT_AT_PANEL));
  h += F(",fail:");  h += jsStr(T(STR_W_SHOT_FAIL));
  h += F(",empty:"); h += jsStr(T(STR_W_SHOT_EMPTY));
  h += F(",save:");  h += jsStr(T(STR_W_LOG_DOWNLOAD));
  h += F(",del:");   h += jsStr(T(STR_W_LOG_DELETE));
  h += F(",copy:");  h += jsStr(T(STR_W_SHOT_COPY));
  h += F(",copied:"); h += jsStr(T(STR_W_SHOT_COPIED));
  h += F(",hint:");  h += jsStr(T(STR_W_SHOT_COPY_HINT));
  h += F(",fw:");    h += jsStr(FW_VERSION);
  h += F(",gap:");   h += String(SAVE_ALL_GAP_MS);
  h += F(",copiedMs:"); h += String(COPIED_SHOWN_MS);
  h += F("};");
}

String screenshotCard() {
  String h;
  h.reserve(7600);
  cardHtml(h);
  cardStrings(h);
  h += F("var shPng={},shIds=[];"
         "function shEl(i){return document.getElementById(i);}"
         "function p2(n){return String(n).padStart(2,'0');}"
         "function shSay(t){shEl('sht').textContent=t;}"
         // Stroke icons: download, copy, trash, and the tick a copy turns into.
         "var ICO={dl:'M12 3v12M7 10l5 5 5-5M5 21h14',"
         "cp:'M11 9h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-8a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2z"
         "M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1',"
         "rm:'M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14M10 11v6M14 11v6',ok:'M5 12l5 5 9-10'};"
         "function shIco(d){return \"<svg viewBox='0 0 24 24' fill='none' stroke='currentColor' "
         "stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'>"
         "<path d='\"+d+\"'/></svg>\";}"
         "function shBtn(cls,key,id,ico,label){return '<button class=\"shi '+cls+'\" data-'+key+'=\"'+id"
         "+'\" title=\"'+label+'\" aria-label=\"'+label+'\">'+shIco(ico)+'</button>';}"
         // The device sends a BMP because it needs no encoder for it; GitHub
         // takes PNG, so the browser redraws it on a canvas and saves that.
         "function shToPng(blob){return createImageBitmap(blob).then(function(bm){"
         "var c=document.createElement('canvas');c.width=bm.width;c.height=bm.height;"
         "c.getContext('2d').drawImage(bm,0,0);"
         "return new Promise(function(res,rej){c.toBlob(function(b){b?res(b):rej(0);},"
         "'image/png');});});}"
         // Each picture is fetched once and kept as a PNG: a BMP is 450 KB.
         "function shFetch(s){if(shPng[s.id])return Promise.resolve();"
         "return fetch('/api/screenshot.bmp?id='+s.id,{cache:'no-store'}).then(function(r){"
         "if(!r.ok)throw 0;return r.blob();}).then(shToPng).then(function(png){"
         "var at=new Date(Date.now()-s.age_s*1000);"
         "shPng[s.id]={blob:png,url:URL.createObjectURL(png),"
         "name:'spoolmanscale-'+SH.fw+'-'+at.getFullYear()+p2(at.getMonth()+1)"
         "+p2(at.getDate())+'-'+p2(at.getHours())+p2(at.getMinutes())+p2(at.getSeconds())+'.png',"
         "label:(s.src==='panel'?SH.panel:SH.web).replace('{t}',at.toLocaleTimeString())};});}"
         "function shItem(id){var p=shPng[id];if(!p)return '';"
         "return '<div><img src=\"'+p.url+'\" alt=\"\" style=\"width:100%;display:block;"
         "border:1px solid var(--line);border-radius:8px\">'"
         "+'<div style=\"display:flex;gap:8px;align-items:center;margin-top:8px\">'"
         "+'<div class=\"note\" style=\"margin:0;flex:1 1 auto;min-width:0\">'+p.label+'</div>'"
         "+shBtn('quiet','sv',id,ICO.dl,SH.save)+shBtn('','cp',id,ICO.cp,SH.copy)"
         "+shBtn('danger','rm',id,ICO.rm,SH.del)+'</div>'"
         "+'<div class=\"note\" data-hn=\"'+id+'\" hidden>'+SH.hint+'</div></div>';}"
         // Newest first. Pictures the device no longer holds lose their PNG.
         "function shRender(){var c=shEl('shl');"
         "Object.keys(shPng).forEach(function(k){if(shIds.indexOf(+k)<0){"
         "URL.revokeObjectURL(shPng[k].url);delete shPng[k];}});"
         "c.innerHTML=shIds.length?shIds.slice().reverse().map(shItem).join('')"
         ":'<div class=\"note\">'+SH.empty+'</div>';"
         "c.querySelectorAll('[data-sv]').forEach(function(b){"
         "b.addEventListener('click',function(){shSave(+b.dataset.sv);});});"
         "c.querySelectorAll('[data-cp]').forEach(function(b){"
         "b.addEventListener('click',function(){shCopy(+b.dataset.cp,b);});});"
         "c.querySelectorAll('[data-rm]').forEach(function(b){"
         "b.addEventListener('click',function(){shDrop('id='+b.dataset.rm);});});"
         "shEl('sha').disabled=shEl('shx').disabled=!shIds.length;}"
         // One at a time: the device answers one request after the other anyway.
         "function shLoad(){return getJson('/api/screenshot/list').then(function(d){"
         "if(!d)return;var l=d.shots||[];shIds=l.map(function(s){return s.id;});"
         "return l.reduce(function(p,s){return p.then(function(){return shFetch(s);});},"
         "Promise.resolve()).then(shRender);}).catch(function(){shSay(SH.fail);});}"
         "function shTake(){var b=shEl('shb');b.disabled=true;b.textContent=SH.busy;shSay('');"
         "post('/api/screenshot','').then(function(r){"
         "if(!r.ok){shSay(SH.fail);return;}return shLoad();})"
         ".finally(function(){b.disabled=false;b.textContent=SH.take;});}"
         // The Blob, not p.url: that object URL belongs to this frame and feeds
         // the <img>; a window of its own, which is where the file is saved
         // inside a backend's frame, cannot read it (app.js, downloadFinish).
         // h is the window of "save all"; a single picture opens its own.
         "function shSave(id,h){var p=shPng[id];if(!p)return;"
         "h=h||downloadStart('sht');if(!h)return;"
         "downloadFinish(h,p.blob,p.name);}"
         // The clipboard takes an image only in a secure context, and the scale
         // speaks plain http: there the picture's own "Copy image" menu still
         // works, so the hint points to it instead of failing quietly.
         "function shCopy(id,b){var p=shPng[id];if(!p)return;"
         "var hint=function(){var n=document.querySelector('[data-hn=\"'+id+'\"]');if(n)n.hidden=false;};"
         "if(!(window.isSecureContext&&navigator.clipboard&&window.ClipboardItem)){hint();return;}"
         "navigator.clipboard.write([new ClipboardItem({'image/png':p.blob})]).then(function(){"
         "shCopyLook(b,ICO.ok,SH.copied,true);"
         "setTimeout(function(){shCopyLook(b,ICO.cp,SH.copy,false);},SH.copiedMs);}).catch(hint);}"
         "function shCopyLook(b,ico,label,ok){b.classList.toggle('ok',ok);b.innerHTML=shIco(ico);"
         "b.title=label;b.setAttribute('aria-label',label);}"
         // One window for all of them, opened here while the click is being
         // handled: opening a window consumes the click, so one per picture
         // would have every window after the first blocked.
         "function shSaveAll(){var h=downloadStart('sht');if(!h)return;"
         "shIds.forEach(function(id,i){"
         "setTimeout(function(){shSave(id,h);},i*SH.gap);});}"
         "function shDrop(q){post('/api/screenshot/drop?'+q,'').then(shLoad);}"
         "shEl('shb').addEventListener('click',shTake);"
         "shEl('sha').insertAdjacentHTML('afterbegin',shIco(ICO.dl));"
         "shEl('shx').insertAdjacentHTML('afterbegin',shIco(ICO.rm));"
         "shEl('sha').addEventListener('click',shSaveAll);"
         "shEl('shx').addEventListener('click',function(){shDrop('all=1');});"
         // A picture taken on the panel shows up once the tab is looked at.
         "document.addEventListener('visibilitychange',function(){"
         "if(!document.hidden)shLoad();});"
         "shLoad();"
         "</script>");
  return h;
}

static void sendBmp(WebServer& srv) {
  const uint32_t id = (uint32_t)srv.arg("id").toInt();
  // Rows leave one at a time, decoded on the way: 1440 bytes of the UI's
  // 480 pixels, about one TCP segment, where the whole file would be 450 KB
  // the internal heap lacks. A picture of a screen at the panel's own size
  // has longer rows and more of them.
  static uint8_t row[SCREENSHOT_BMP_ROW_MAX_BYTES];
  uint8_t header[SCREENSHOT_BMP_HEADER_BYTES];
  int w = 0, h = 0;
  if (!screenshotSize(id, w, h) || !screenshotBmpHeader(id, header) ||
      !screenshotBmpRow(id, 0, row)) {
    srv.send(404, "text/plain", "No such screenshot");
    return;
  }
  const size_t row_bytes = (size_t)w * 3;
  srv.sendHeader("Cache-Control", "no-store");
  srv.setContentLength(sizeof(header) + row_bytes * h);
  srv.send(200, "image/bmp", "");
  srv.sendContent((const char*)header, sizeof(header));
  srv.sendContent((const char*)row, row_bytes);
  for (int r = 1; r < h; r++) {
    if (!screenshotBmpRow(id, r, row)) break;
    srv.sendContent((const char*)row, row_bytes);
  }
}

static void sendList(WebServer& srv) {
  String json = "{\"max\":";
  json += String(SCREENSHOT_MAX);
  json += ",\"shots\":[";
  ScreenshotInfo info;
  for (int i = 0; screenshotInfoAt(i, info); i++) {
    if (i) json += ',';
    json += "{\"id\":";     json += String(info.id);
    json += ",\"age_s\":";  json += String(info.age_ms / MS_PER_S);
    json += ",\"bytes\":";  json += String(info.bytes);
    json += ",\"src\":\"";
    json += info.source == SCREENSHOT_FROM_PANEL ? "panel" : "web";
    json += "\"}";
  }
  json += "]}";
  srv.send(200, "application/json", json);
}

void screenshotRoutes(WebServer& srv) {
  // POST /api/screenshot -> takes a picture of the display now
  srv.on("/api/screenshot", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    const uint32_t id = screenshotTake(SCREENSHOT_FROM_WEB);
    if (!id) {
      srv.send(503, "application/json", "{\"error\":\"Screenshot failed, no memory\"}");
      return;
    }
    srv.send(200, "application/json", String("{\"id\":") + id + "}");
  });

  // GET /api/screenshot/list -> the held pictures, oldest first
  srv.on("/api/screenshot/list", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    sendList(srv);
  });

  // GET /api/screenshot.bmp?id=N -> one picture, 404 when it is not held
  srv.on("/api/screenshot.bmp", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    sendBmp(srv);
  });

  // GET /api/glass.raw -> the frame as the panel shows it this moment, on a
  // board that keeps one: RGB565, little endian, in rows, its size in the
  // X-Glass header. A screenshot is what the UI drew; this is what reached
  // the glass, enlargement and dimming included, and the only picture there
  // is of a screen at the panel's own resolution.
  srv.on("/api/glass.raw", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    const uint16_t* glass = displayGlass();
    if (!glass) { srv.send(404, "text/plain", "This board keeps no frame"); return; }
    static constexpr size_t GLASS_CHUNK_BYTES = 4096;
    const size_t total = (size_t)displayNativeW() * displayNativeH() * sizeof(uint16_t);
    srv.sendHeader("X-Glass", String(displayNativeW()) + "x" + displayNativeH());
    srv.setContentLength(total);
    srv.send(200, "application/octet-stream", "");
    const char* p = (const char*)glass;
    for (size_t off = 0; off < total; off += GLASS_CHUNK_BYTES) {
      const size_t n = (total - off < GLASS_CHUNK_BYTES) ? total - off : GLASS_CHUNK_BYTES;
      srv.sendContent(p + off, n);
    }
  });

  // POST /api/screenshot/drop?id=N or ?all=1 -> frees the PSRAM they hold
  srv.on("/api/screenshot/drop", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    if (srv.hasArg("all")) {
      screenshotDropAll();
    } else {
      screenshotDrop((uint32_t)srv.arg("id").toInt());
    }
    srv.send(200, "application/json", "{\"ok\":true}");
  });
}
