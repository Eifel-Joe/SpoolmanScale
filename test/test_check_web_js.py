"""Check that scripts/check_web_js.py reaches the script of every web page, the printer page's too (Punkteliste f13, review of L1).

The checker replays the `x += ...` statements of a file in the order they stand in it. The printer page writes its <script> and </script> in pageScript() and the script itself in seven helpers, `scriptStrings(h)` and so on, one of them in another file (webPrinterHostScript() in web_printer_host.cpp). The helpers stand above pageScript(), so their text landed before the <script> tag, the block between the tags was empty, and node never saw a line of that page: a syntax error there would have killed the page's script without a finding. A call of a helper that only appends to its one String now brings the helper's text to the place of the call, from whichever file under src/web it comes.

Without node: the test looks at the blocks the checker hands to node, not at node's verdict (scripts/check.sh runs that)."""
import shutil
import tempfile
from pathlib import Path

import klipper_src as ks
import check_web_js as cw

ks.assert_ci_step("test_check_web_js.py")

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


WEB = ks.src / "web"
files = sorted(str(p) for p in WEB.rglob("*.cpp"))
helpers = cw.collect_helpers(files)

# 1. The printer page: its script block holds the helpers' script, the one
# from the other file as well.
page = str(WEB / "pages" / "page_printer.cpp")
blocks = [body for _, _, body in cw.script_blocks(page, helpers)]
need(len(blocks) == 1, f"src/web/pages/page_printer.cpp: {len(blocks)} script block(s) for node, expected 1")
joined = "\n".join(blocks)
for needle, origin in (("post('/api/printer/link'", "scriptHandlers()"),
                       ("function saveHost(", "webPrinterHostScript() in web_printer_host.cpp"),
                       ("load();", "the end of scriptHandlers()")):
    need(needle in joined, f"src/web/pages/page_printer.cpp: the script block lacks {needle!r} from {origin}")

# 2. Every file that writes a <script> tag hands node at least one block with
# something in it, so a page whose script went missing shows up here.
for path in files:
    bufs = cw.reconstruct_inlined(path, helpers)
    if not any("<script" in text for text in bufs.values()):
        continue
    found = [body for _, _, body in cw.script_blocks(path, helpers) if body.strip()]
    need(found, f"{Path(path).relative_to(ks.root).as_posix()}: writes <script> but no block reaches node")

# 3. A small tree of its own: the helper above the page, the one in another
# file, a helper calling a helper, in the order of the calls; a function with
# a second parameter is replayed as before.
tmp = Path(tempfile.mkdtemp(prefix="cwj-"))
try:
    (tmp / "other.cpp").write_text(
        'void otherScript(String& h) {\n  h += F("var c=3;");\n}\n', encoding="utf-8")
    (tmp / "page.cpp").write_text(
        'static void first(String& h) {\n  h += F("var a=1;");\n  second(h);\n}\n'
        'static void second(String& h) {\n  h += "var b=2;";\n}\n'
        'static void rows(String& h, int n) {\n  h += F("<p>x</p>");\n}\n'
        'static void pageScript(String& h) {\n  h += F("<script>");\n  first(h);\n'
        '  otherScript(h);\n  h += F("</script>");\n}\n'
        'static String body() {\n  String h;\n  rows(h, 2);\n  pageScript(h);\n  return h;\n}\n',
        encoding="utf-8")
    tree = [str(tmp / "other.cpp"), str(tmp / "page.cpp")]
    small = cw.collect_helpers(tree)
    got = [body for _, _, body in cw.script_blocks(str(tmp / "page.cpp"), small)]
    need(got == ["var a=1;var b=2;var c=3;"],
         f"fixture: the block for node is {got!r}, expected ['var a=1;var b=2;var c=3;']")
    need("rows" not in small, "fixture: rows(String& h, int n) taken for a helper, it has two parameters")
    plain = cw.reconstruct(str(tmp / "page.cpp"))
    need("<p>x</p>" in plain.get("h", ""), "fixture: reconstruct() lost the text of rows(), it must stay as it was")
finally:
    shutil.rmtree(tmp, ignore_errors=True)

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_check_web_js: ok")
