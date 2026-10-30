"""Read the source tree without a compiler and check the page "Klipper" of the browser (web/pages/page_klipper.cpp) and the job behind its connection test: the page is in the tab strip after the label printer, behind the configuration gate, and only where Klipper printers can be used; the list never carries a key, only whether there is one; saving starts the test, deleting asks first, every fetch has a catch; every verdict of the test has its text in German, English and French; and the job runs the probe on a copy, without touching the settings."""
import re

import klipper_src as ks

PAGE = "web/pages/page_klipper.cpp"

ks.assert_ci_step("test_klipper_page.py")

page = ks.source_of(PAGE)
page_code = ks.collapse(page.code)

# 1. In the table of pages: after the label printer; the gate and the question.
pages = ks.squeeze(ks.source_of("web/web_pages.cpp").code)
assert "externconstWebPagePAGE_KLIPPER;" in pages, "src/web/web_pages.cpp: PAGE_KLIPPER is not declared"
assert "&PAGE_PRINTER,&PAGE_KLIPPER," in pages, "src/web/web_pages.cpp: &PAGE_KLIPPER does not follow &PAGE_PRINTER in WEB_PAGES"

init = re.search(r"const\s+WebPage\s+PAGE_KLIPPER\s*=\s*\{([^{}]*)\}", page.code_str)
assert init, f"src/{PAGE}: const WebPage PAGE_KLIPPER = {{ ... }} is not there"
parts = [p.strip() for p in init.group(1).split(",")]
assert len(parts) == 6, f"src/{PAGE}: PAGE_KLIPPER does not have the six members of a WebPage"
assert parts[0] == '"/klipper"', f"src/{PAGE}: the path of PAGE_KLIPPER is {parts[0]}, not \"/klipper\""
assert parts[2] == "GATE_CONFIG", f"src/{PAGE}: the gate of PAGE_KLIPPER is {parts[2]}, not GATE_CONFIG"
applies = parts[3]
assert applies != "nullptr", f"src/{PAGE}: PAGE_KLIPPER applies everywhere"
if applies != "klipperAvailable":
    assert re.search(r"\breturn\s+klipperAvailable\s*\(\s*\)\s*;", ks.body_of(PAGE, applies)), \
        f"src/{PAGE}: {applies}() is not klipperAvailable()"
assert '#include "services/klipper_printers.h"' in page.text, f"src/{PAGE}: services/klipper_printers.h is not included"

# 2. The tab text.
rows = ks.lang_rows()
tab_ids = ks.lang_ids_with_german(rows, r"Klipper")
assert tab_ids, "src/lang.cpp: no row with the German text 'Klipper' for the tab"
label_fn = parts[1]
assert any(f"T({i})" in ks.body_of(PAGE, label_fn) for i in tab_ids), \
    f"src/{PAGE}: {label_fn}() does not return the text 'Klipper' from the string table"

# 3. The routes.
GET = "/api/klipper"
POSTS = ("/api/klipper/save", "/api/klipper/delete", "/api/klipper/test", "/api/klipper/loc")
assert "HTTP_GET" in ks.route_text(PAGE, GET), f"src/{PAGE}: {GET} is not a GET"
for path in (GET,) + POSTS:
    text = ks.route_text(PAGE, path)
    assert re.search(r"\bwebRequire\s*\(\s*\w+\s*,\s*GATE_CONFIG\b", text), \
        f"src/{PAGE}: {path} does not check GATE_CONFIG"
for path in POSTS:
    assert "HTTP_POST" in ks.route_text(PAGE, path), f"src/{PAGE}: {path} is not a POST"

# What is read from the request and what is written to the page, by name.
lits = [lit.replace('\\"', '"') for lit in ks.literals_of(PAGE)]
for arg in ("id", "name", "host", "port", "key", "keyset", "keydel", "on"):
    assert arg in lits, f"src/{PAGE}: the argument '{arg}' is not read"
for member in ("loc", "room", "printers", "id", "name", "host", "port", "key", "layout", "fields",
               "test", "test_text", "tested_at"):
    assert any(lit == member or f'"{member}"' in lit for lit in lits), \
        f"src/{PAGE}: the list does not carry '{member}'"
for call in ("klipperStoreSave", "klipperStoreDelete", "klipperSetLocSwitch"):
    assert re.search(rf"\b{call}\s*\(", page_code), f"src/{PAGE}: {call}() is never called"

# The list never carries a key: "key" is a yes or no. The store hands a key out
# through klipperStoreKey(); to know whether there is one the list has to ask,
# but what it gets back goes nowhere: outside the test route (and what that
# calls, to copy the key to the job) a buffer that can hold a key is only
# looked at for emptiness, never written into an answer or a log line.
assert not any(re.search(r'"key"\s*:\s*"', lit) for lit in lits), \
    f"src/{PAGE}: a literal writes \"key\" as a string, the list may only say whether there is one (true or false)"
test_spans = ks.route_spans(PAGE, "/api/klipper/test")
funcs = ks.cc.find_functions(page.code)
used_by_test = {name for name, _, _ in funcs
                if any(re.search(rf"\b{name}\s*\(", page.code[a:b]) for a, b in test_spans)}
route_spans = [s for path in (GET,) + POSTS for s in ks.route_spans(PAGE, path)]
# The places to look at: each route but the test, and each function that holds
# no route and is not part of the test.
units = [s for s in route_spans if s not in test_spans]
units += [(a, b) for name, a, b in funcs
          if name not in used_by_test and not any(a <= x < b for x, _ in route_spans)]
every_buffer = set()
for a, b in units:
    text = page.code[a:b]
    names = set(re.findall(r"\bchar\s+(\w+)\s*\[[^\]]*KL_KEY_MAX", text))
    for _, args in ks.calls(text, "klipperStoreKey"):
        assert len(args) >= 2 and re.fullmatch(r"&?\w+", args[1].strip()), \
            f"src/{PAGE}: klipperStoreKey() outside the test route is not given a plain buffer ({args})"
        names.add(args[1].strip().lstrip("&"))
    every_buffer |= names
    rest = text
    for start, end in ks.call_spans(text, "klipperStoreKey")[::-1]:
        rest = rest[:start] + " " + rest[end:]
    for buf in names:
        for m in re.finditer(rf"\b{buf}\b", rest):
            before, after = rest[max(0, m.start() - 8):m.start()], rest[m.end():m.end() + 3]
            # Looked at for emptiness (buf[0], strlen(buf)), or declared (char buf[...]).
            assert after == "[0]" or before.endswith(("strlen(", "char ")), \
                f"src/{PAGE}: the key buffer '{buf}' is used for more than telling whether there is a key ('{rest[max(0, m.start() - 20):m.end() + 20]}')"
# And no answer carries one, the test route's included.
every_buffer |= set(re.findall(r"\bchar\s+(\w+)\s*\[[^\]]*KL_KEY_MAX", page.code))
every_buffer |= {args[1].strip().lstrip("&") for _, args in ks.calls(page.code, "klipperStoreKey") if len(args) >= 2}
for buf in every_buffer:
    for _, args in ks.calls(page.code, "send"):
        assert not re.search(rf"\b{buf}\b", " ".join(args)), f"src/{PAGE}: an answer is sent with the key buffer '{buf}'"

# 4. The test: started by a POST (202), asked again at the same address for the
# answer, and the answer written on the loop task by the one who collects it.
test_text = ks.route_text(PAGE, "/api/klipper/test")
for token in ("webJobStart(", "WJ_KLIPPER_TEST", "202", "webJobState(", "WJS_DONE", "webJobTake(", "klipperStoreSetTest("):
    assert token in test_text, f"src/{PAGE}: the route /api/klipper/test lacks {token}"
assert "klipperScaleSpoolman(" in page_code, f"src/{PAGE}: the job is not handed the scale's Spoolman (klipperScaleSpoolman())"

# 5. The script of the page. Every fetch has a catch (check.sh holds the whole
# page to it as well, but a page without a single fetch would pass that).
raw = page.text
assert raw.count("fetch(") <= raw.count(".catch("), f"src/{PAGE}: {raw.count('fetch(')} fetch( but only {raw.count('.catch(')} .catch("
# Only the literals that are JavaScript: the route paths of routes() would
# otherwise be glued to the end of the last function of the script.
script = " ".join(lit for lit in lits if re.search(r"function|fetch\(|=>|confirm\(|\.then\(|document\.|\$\(", lit))
for path in (GET,) + POSTS:
    assert path in script, f"src/{PAGE}: the script never asks {path}"

# The named functions of the script, each with its text up to the next one.
chunks = re.split(r"(?=function\s+\w+\s*\()", script)
named = {}
for chunk in chunks:
    m = re.match(r"function\s+(\w+)\s*\(", chunk)
    if m:
        named[m.group(1)] = chunk


def pieces_with(url):
    """The pieces of the script that name the address: the named functions that do,
    and the rest of the script when the address stands outside any of them."""
    out = [(n, t) for n, t in named.items() if url in t]
    rest = chunks[0] if chunks and not re.match(r"function\s+\w+\s*\(", chunks[0]) else ""
    if url in rest:
        out.append(("(outside a function)", rest))
    return out


saves = pieces_with("/api/klipper/save")
assert saves, f"src/{PAGE}: the script never saves"
tests = {n for n, t in named.items() if "/api/klipper/test" in t}
for name, text in saves:
    ok = "/api/klipper/test" in text or any(re.search(rf"\b{t}\s*\(", text) for t in tests)
    assert ok, f"src/{PAGE}: {name}() saves and does not start the test afterwards"
deletes = pieces_with("/api/klipper/delete")
assert deletes, f"src/{PAGE}: the script never deletes"
# The question is ask() of /app.js, the house form of every page (page_backend,
# page_logs, page_tags): confirm() draws the browser's box at the top of the
# window, and inside a backend's frame it names the scale's address as an
# "embedded page". Either one counts.
for name, text in deletes:
    assert re.search(r"\b(?:ask|confirm)\s*\(", text), f"src/{PAGE}: {name}() deletes without asking (ask() or confirm())"

# 6. Every verdict of the test, and what the printer has, in words in all three languages.
TEXTS = {
    "KT_OK": r"Verbunden: .*, Spoolman verbunden, derselbe Spoolman",
    "KT_UNREACHABLE": r"Nicht erreichbar - Adresse und Port prüfen",
    "KT_ACCESS": r"Zugang verweigert - Adresse der Waage in Moonraker als vertraut eintragen \(trusted_clients\) oder API-Schlüssel angeben",
    "KT_KLIPPY": r"Klipper nicht bereit \(.*\)",
    "KT_NO_SPOOLMAN": r"Spoolman ist in Moonraker nicht eingerichtet \(\[spoolman\]\)",
    "KT_SM_OFFLINE": r"Spoolman eingerichtet, aber nicht verbunden",
    "KT_OTHER_SPOOLMAN": r"Anderer Spoolman als die Waage \(.*\) - Zuordnen gesperrt",
    "KT_BAD_ANSWER": r"Unerwartete Antwort \(HTTP .*\)",
}
LAYOUTS = (r"Happy Hare, .* Fächer", r"AFC, .* Fächer", r"Werkzeugwechsler, .* Werkzeuge", r"ein Extruder",
           r"unbekanntes System .*, ein Feld")
NOTES = (r"Werkzeug-Makros ohne Variable spool_id - nur die aktive Spule \(Anleitung von Mainsail oder Fluidd\)",
         r"mehr als 16 Felder - die ersten 16")
ids_of = {}
for verdict, german in TEXTS.items():
    found = ks.lang_ids_with_german(rows, german)
    assert len(found) == 1, f"src/lang.cpp: {len(found)} rows with the text of {verdict} ({german}), expected 1"
    ids_of[verdict] = found[0]
for german in LAYOUTS + NOTES:
    found = ks.lang_ids_with_german(rows, german)
    assert len(found) == 1, f"src/lang.cpp: {len(found)} rows with the text '{german}', expected 1"
    ids_of[german] = found[0]
for sid in list(ids_of.values()) + tab_ids[:1]:
    ks.assert_row_complete(rows, sid, "src/lang.cpp")
    assert ks.enum_has(sid), f"src/lang.h: {sid} is not in the enum"
    assert sid in page_code, f"src/{PAGE}: {sid} is never used"
for verdict, sid in ((v, ids_of[v]) for v in TEXTS):
    # The verdict is told to its text: the name, then the id, before the next ;
    assert re.search(rf"\b{verdict}\b[^;{{}}]*?\b{sid}\b", page_code), \
        f"src/{PAGE}: {verdict} is not paired with {sid}"

# 7. The job behind the test.
jobs_h = ks.code_of("web/web_jobs.h")
assert "WJ_KLIPPER_TEST" in jobs_h, "src/web/web_jobs.h: WJ_KLIPPER_TEST is not in the enum"
jobs = ks.source_of("web/web_jobs.cpp")
assert "case WJ_KLIPPER_TEST" in ks.body_of("web/web_jobs.cpp", "webJobTask"), \
    "src/web/web_jobs.cpp: webJobTask() does not run WJ_KLIPPER_TEST"
# The default of webJobTalksToGithub() is "yes, it is a GitHub job": a new kind
# that is not named in the list of those that go to the local network would be
# held to the TLS floor and to the wait for the daily check.
talks = ks.body_of("web/web_jobs.cpp", "webJobTalksToGithub")
assert re.search(r"case WJ_KLIPPER_TEST:[^;]*?return false;", talks), \
    "src/web/web_jobs.cpp: webJobTalksToGithub() does not say that WJ_KLIPPER_TEST does not talk to GitHub"
probing = [name for name, a, b in ks.cc.find_functions(jobs.code) if re.search(r"\bmoonrakerProbe\s*\(", jobs.code[a:b])]
assert probing, "src/web/web_jobs.cpp: no function runs moonrakerProbe()"
for name, a, b in ks.cc.find_functions(jobs.code):
    if name in probing:
        text = jobs.code[a:b]
        assert not re.search(r"\bprefs(?:Put|Get)\w*\s*\(", text), f"src/web/web_jobs.cpp: {name}() touches the settings, the job runs on its own task"
        assert not re.search(r"\bklipperStore\w+\s*\(", text), f"src/web/web_jobs.cpp: {name}() touches the store, the job works on a copy"

print("test_klipper_page: ok")
