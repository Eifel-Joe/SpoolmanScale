"""Read the source tree without a compiler and check the acceptance build's
three calls of v0.8.7-beta.2 (Spec 5.3 AA, "Server-Antwort, Aufkleber und
Fenster"):

  POST /api/sim/server-answer?code=<400-599>&count=<1-50>[&path=<word>][&retry=<s>] | code=off
      the next requests of the scale to its own server (only those whose path
      holds the word) get that answer, without being sent; a 429 with its wait
  POST /api/sim/tag?uid=<14 hex> | uid=off
      a made-up, empty NTAG215 lies on the reader and goes again; it is never
      written
  POST /api/sim/popup?name=tare-missing
      opens "Leergewicht fehlt" for the picture

The refusals of a server (401, 402, 429) and a tag that nobody knows cannot be
had at Jörg's scale without harm, and the acceptance is to be done by Claude
(Jörg 04.11.2026). So: acceptance build only, behind the maintenance gate,
every change in the log. The answer is given in BackendHttp, where every
request to the server passes, only for the server's own address, and a refusal
is recorded as a real one would be (Retry-After included); the tag in
hardware/nfc.cpp, the one place that talks to the reader. Which methods of
BackendHttp ask first is test_build_variants.py's part."""
import re

import klipper_src as ks

ks.assert_ci_step("test_acceptance_sim_server.py")

DOC = "web/web_doc.cpp"
problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


doc_text = ks.source_of(DOC).text
accept_at = doc_text.find("#if defined(SSC_ACCEPTANCE)")


def route(path):
    try:
        return ks.squeeze(ks.route_text(DOC, path))
    except AssertionError as e:
        problems.append(str(e))
        return ""


for path, calls in (("/api/sim/server-answer", ("docAnswerSet(", "docAnswerOff(", "simAnswerArgsOk(")),
                    ("/api/sim/tag", ("docTagSet(", "docTagOff(")),
                    ("/api/sim/popup", ("showTareMissingPopup(",))):
    text = route(path)
    if not text:
        continue
    need(re.search(r'srv\.on\("' + re.escape(path) + r'",HTTP_POST', text), f"src/{DOC}: {path} is not a POST")
    need("webRequire(srv,GATE_MAINT" in text, f"src/{DOC}: {path} is not behind the maintenance gate")
    gate = text.find("webRequire(")
    for call in calls:
        at = text.find(call)
        need(at > gate >= 0, f"src/{DOC}: {path} does not call {call.rstrip('(')}() after the gate")
    need("400" in text, f"src/{DOC}: {path} does not refuse a bad argument with 400")
    need("logSD" in text, f"src/{DOC}: {path} does not log what it did")
    need(doc_text.find(path) > accept_at >= 0, f"src/{DOC}: {path} is not inside the acceptance build")

# The state: in RAM, shared by both cores (a backend job asks on the other one).
STATE = "app/doc_build.cpp"
state = ks.code_of(STATE)
for name in ("docAnswerSet", "docAnswerOff", "docAnswerTake", "docTagSet", "docTagOff", "docTagUid",
             "docTagPage", "docTagActive"):
    need(re.search(r"\b" + name + r"\s*\([^)]*\)\s*\{", state), f"src/{STATE}: {name}() is not defined")
take = ks.squeeze(ks.body_of(STATE, "docAnswerTake")) if "docAnswerTake" in state else ""
need("portENTER_CRITICAL(" in take and "portEXIT_CRITICAL(" in take and "simAnswerTake(" in take,
     f"src/{STATE}: docAnswerTake() does not take the answer under a lock with simAnswerTake()")
page = ks.squeeze(ks.body_of(STATE, "docTagPage")) if "docTagPage" in state else ""
need("simNtagPage(" in page, f"src/{STATE}: docTagPage() does not read the made-up tag through simNtagPage()")

# Where the answer is given: BackendHttp, for the server's own address only.
HTTP = "services/backend_http.cpp"
http = ks.code_of(HTTP)
answered = ks.squeeze(ks.body_of(HTTP, "docAnswered")) if re.search(r"\bdocAnswered\s*\(", http) else ""
need(answered, f"src/{HTTP}: BackendHttp::docAnswered() is missing")
if answered:
    need("backendBaseUrl()" in answered and "_host" in answered,
         f"src/{HTTP}: docAnswered() does not keep to the server's own address")
    need("docAnswerTake(_uri.c_str()" in answered, f"src/{HTTP}: docAnswered() does not hand docAnswerTake() the path")
    need("serverRefusalRecord(" in answered, f"src/{HTTP}: docAnswered() does not record a refusal as a real one")
    need("_size=0" in answered, f"src/{HTTP}: docAnswered() leaves a body to read that was never sent")
    need("logSDf(" in answered, f"src/{HTTP}: docAnswered() does not log the request it answered")

# The tag: the reader's door answers with it, and nothing is written to it.
NFC = "hardware/nfc.cpp"
nfc_code = ks.code_of(NFC)
need(not re.search(r"\bSSC_ACCEPTANCE\b", ks.source_of(NFC).code_str),
     f"src/{NFC}: must not mention SSC_ACCEPTANCE (the stubs in app/doc_build.h carry a release)")
read = ks.squeeze(ks.body_of(NFC, "nfcReadPassiveTarget"))
need(read.find("docTagUid(") >= 0 and read.find("docTagUid(") < read.find("readPassiveTargetID("),
     f"src/{NFC}: nfcReadPassiveTarget() does not give the made-up tag before it asks the reader")
ntag = ks.squeeze(ks.body_of(NFC, "nfcReadNtagPage"))
need(ntag.find("docTagPage(") >= 0 and ntag.find("docTagPage(") < ntag.find("ntag2xx_ReadPage("),
     f"src/{NFC}: nfcReadNtagPage() does not read the made-up tag first")
for name in ("nfcWriteNtagPage", "nfcWriteMifareBlock", "nfcReadMifareSector", "nfcReadMifareBlock",
             "nfcAuthMifareSector"):
    body = ks.squeeze(ks.body_of(NFC, name))
    need("docTagActive()" in body, f"src/{NFC}: {name}() does not stop while the made-up tag lies on the reader")

# The rules have no switch (the host test compiles them without one).
for rel in ("services/sim_answer_rules.h", "services/sim_tag_rules.h"):
    path = ks.src / rel
    need(path.is_file(), f"src/{rel}: file is missing")
    if path.is_file():
        need(not re.search(r"\bSSC_(ACCEPTANCE|DOC_BUILD)\b", path.read_text(encoding="utf-8")),
             f"src/{rel}: the rules must not depend on a switch")

# Only web_doc.cpp spells the routes (the release run looks for /api/sim/ texts).
for path in sorted(ks.src.rglob("*")):
    if path.suffix in (".h", ".cpp") and path.relative_to(ks.src).as_posix() != DOC:
        text = path.read_text(encoding="utf-8", errors="replace")
        for p in ("/api/sim/server-answer", "/api/sim/tag", "/api/sim/popup"):
            if re.search(re.escape(p) + r"(?![-\w])", text):
                problems.append(f"src/{path.relative_to(ks.src).as_posix()}: spells {p}")

assert not problems, "\n".join(problems)
print("ok: made-up server answer, tag and popup, acceptance build only, behind the gate")
