"""Run the firmware's request, cache-key and WebUI logic without ESP hardware."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
sys.path.insert(0, str(root / "scripts"))
from check_web_js import blocks_for, reconstruct


def function(path, signature):
    text = (root / path).read_text()
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2]


server = "src/app/label_server.cpp"
labels = "src/services/filaman_labels.cpp"
page = "src/web/pages/page_label.cpp"
key = re.search(r"struct LabelKey \{.*?\n};", (root / server).read_text(), re.S)[0]
source = r'''
#include "services/filaman_labels.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T n) : std::string(std::to_string(n)) {}
};
#define F(x) x
#define LS_RENDER_TIMEOUT_MS 15000
#define FM_LABEL_PATH_LEN 192
struct LabelPrinterConfig { int model; uint16_t media_width_mm, media_length_mm; };
struct Profile { uint16_t dpi; };
Profile labelPrinterProfile(int) { return {203}; }
uint16_t labelPrinterRasterWidth(int, uint16_t) { return 576; }
uint32_t backendGeneration() { return 1; }
int labelServerPresetId() { return 42; }
enum LabelServerListState { LS_UNKNOWN, LS_LOADING, LS_READY, LS_ABSENT, LS_FAILED };
enum LabelServerPreviewState { LSP_NONE, LSP_LOADING, LSP_READY, LSP_FAILED };
FilamanLabelPresetList presets{};
LabelServerListState labelServerList(const FilamanLabelPresetList** out) {
  *out = &presets; return LS_READY;
}
bool labelServerInUse() { return false; }
String jsonEsc(const char* s) { return s; }
LabelServerPreviewState labelServerPreview(const LabelPrinterConfig&, int,
                                           const LabelRaster**, const char**) { return LSP_NONE; }
uint32_t labelServerPreviewRev() { return 1; }
std::string requested;
struct BackendHttp {
  bool begin(const String& url) { requested = url; return true; }
  void setTimeout(uint32_t) {}
  void collectHeaders(const char**, size_t) {}
  int GET() { return 503; }
  void end() {}
  String header(const char*) { return ""; }
};
bool hasBase(const char* base) { return base && base[0]; }
void filamanAddApiKey(BackendHttp&, const char*) {}
void readReason(BackendHttp&, int, char*, size_t) {}
bool rasterShape(BackendHttp&, LabelRaster*) { return false; }
bool readPixels(BackendHttp&, LabelRaster*) { return false; }
void labelRasterFree(LabelRaster*) {}
void logSDf(const char*, ...) {}
unsigned long millis() { return 1; }
const char* RASTER_HEADERS[] = {"X-Image-Width"};
const size_t RASTER_HEADER_COUNT = 1;
'''
source += key
for signature in ("static bool sameKey(", "static bool keyFor(", "static FilamanLabelRequest requestFor("):
    source += "\n" + function(server, signature)
source += "\n" + function(labels, "int filamanLabelRender(")
source += "\n" + function(page, "static void serverJson(")
source += r'''
int main() {
  const LabelPrinterConfig landscape{1, 40, 30}, portrait{1, 30, 40};
  LabelKey wide{}, tall{}, repeat{};
  assert(keyFor(landscape, 7, &wide));
  assert(keyFor(portrait, 7, &tall));
  assert(keyFor(landscape, 7, &repeat));
  assert(sameKey(wide, repeat));
  assert(!sameKey(wide, tall)); // M220 uses the same 576-dot row for both rolls.
  LabelRaster raster{};
  for (const auto& item : {std::make_pair(wide, "landscape"), std::make_pair(tall, "portrait")}) {
    auto request = requestFor(item.first); // No preset list is needed after reboot.
    request.base_url = "http://filaman";
    assert(filamanLabelRender(request, &raster, nullptr, 0) == 503);
    assert(requested.find("orientation=" + std::string(item.second)) != std::string::npos);
    assert(requested.find("format=mono1&dpi=203&width=576&align=left") != std::string::npos);
    assert(requested.find("preset_id=42") != std::string::npos);
  }
  struct FitCase { uint16_t w, h; bool fits, rotated; };
  const FitCase cases[] = {{40, 30, true, false}, {30, 40, true, true},
                           {40, 40, false, false}, {50, 30, false, false},
                           {0, 0, false, false}, {20, 30, false, false}};
  presets.count = 1;
  for (const auto& item : cases) {
    presets.items[0] = {42, "Preset", item.w, item.h};
    String json;
    serverJson(json, landscape, 7);
    assert(json.find(item.fits ? "\"fit\":true" : "\"fit\":false") != std::string::npos);
    assert(json.find(item.rotated ? "\"rotated\":true" : "\"rotated\":false") != std::string::npos);
  }
  presets.items[0] = {42, "Square", 40, 40};
  String json;
  serverJson(json, {1, 40, 40}, 7);
  assert(json.find("\"fit\":true") != std::string::npos);
  assert(json.find("\"rotated\":false") != std::string::npos);
  presets.items[0] = {42, "Unknown", 0, 0};
  json.clear();
  serverJson(json, {1, 0, 0}, 7);
  assert(json.find("\"fit\":false") != std::string::npos);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# Execute the emitted selector code; only browser DOM primitives are substituted.
script = next(body for _, body in blocks_for("h", reconstruct(str(root / page))["h"]))
script = script.split("document.querySelectorAll('#lpre .btab').forEach(function(b){b.addEventListener", 1)[0]
script += r'''
const assert = require('node:assert/strict');
const elements = {};
function $(id) {
  return elements[id] ||= {style:{}, dataset:{}, options:[], appendChild(o){this.options.push(o);}};
}
const document = {createElement(){return {};}};
L.scale = 'Scale'; L.rotated = 'fits rotated';
srcRow({fm:'ready',source:2,presets:[
  {id:1,name:'Direct',size:'40 x 30 mm',fit:true,rotated:false},
  {id:2,name:'Portrait',size:'30 x 40 mm',fit:true,rotated:true},
  {id:3,name:'Oversize',size:'50 x 40 mm',fit:false,rotated:false}
]});
assert.equal(elements.lsrc.options[1].textContent, 'Direct');
assert.equal(elements.lsrc.options[2].disabled, false);
assert.match(elements.lsrc.options[2].textContent, /fits rotated/);
assert.equal(elements.lsrc.options[3].disabled, true);
'''
subprocess.run(["node"], input=script, text=True, check=True)
print("Label media orientation tests passed")
