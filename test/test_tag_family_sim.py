"""Read the source tree without a compiler and check the acceptance build's call for the tag family memory (Spec 5.3 AA, v0.8.7-beta.1): POST /api/sim/tag-family?uid=<8 hex>&family=bambu|creality|snapmaker|forget|show tells the memory a made-up tag the way a placed tag does, forgets it again, or loads the memory and shows it.

The memory is read only when a tag lies on the reader with Creality or Snapmaker tags on, and the acceptance build cannot fake a tag. To show at the 7B that the stored index of "Neu aus Datenbank" and the memory no longer overwrite each other (Punkteliste n69), Claude needs both in the log after a restart without Jörg placing a tag. So the call works only while the memory is in use, as the scale's own placements do, sits behind the maintenance gate in the acceptance build only, and writes what it did and the state of the memory to the log."""
import re

import klipper_src as ks

ks.assert_ci_step("test_tag_family_sim.py")

DOC = "web/web_doc.cpp"
TF = "services/tag_family.cpp"
TF_H = "services/tag_family.h"
PATH = "/api/sim/tag-family"

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


doc_text = ks.source_of(DOC).text
route = ""
try:
    route = ks.squeeze(ks.route_text(DOC, PATH))
except AssertionError as e:
    problems.append(str(e))

if route:
    need(re.search(r'srv\.on\("/api/sim/tag-family",HTTP_POST', route), f"src/{DOC}: {PATH} is not a POST")
    need("webRequire(srv,GATE_MAINT" in route, f"src/{DOC}: {PATH} is not behind the maintenance gate")
    gate = route.find("webRequire(")
    for call in ("tagFamilySet(", "tagFamilyGet("):
        at = route.find(call)
        need(at > gate >= 0, f"src/{DOC}: {PATH} does not call {call} after the gate")
    need("tagFamilyInUse()" in route and "409" in route,
         f"src/{DOC}: {PATH} does not refuse with 409 while Creality and Snapmaker tags are off")
    need("400" in route, f"src/{DOC}: {PATH} does not refuse a bad uid or family with 400")
    need("TAG_FAMILY_UNKNOWN" in route, f"src/{DOC}: {PATH} cannot forget a tag (TAG_FAMILY_UNKNOWN)")
    for word in ("bambu", "creality", "snapmaker", "forget", "show"):
        need(f'"{word}"' in route, f'src/{DOC}: {PATH} does not know family={word}')
    need("logSDf(" in route and "tagFamilyStatus(" in route,
         f"src/{DOC}: {PATH} does not log what it did with the state of the memory")
    need(doc_text.find(PATH) > doc_text.find("#if defined(SSC_ACCEPTANCE)") >= 0,
         f"src/{DOC}: {PATH} is not inside the acceptance build")

# The memory says whether it is in use and how it stands, for the call.
h = ks.code_of(TF_H)
need("bool tagFamilyInUse()" in h, f"src/{TF_H}: tagFamilyInUse() is not declared")
need(re.search(r"void tagFamilyStatus\(char\s*\*\s*\w*,\s*size_t", h), f"src/{TF_H}: tagFamilyStatus(char*, size_t) is not declared")
status = ks.squeeze(ks.body_of(TF, "tagFamilyStatus", strings=True)) if "tagFamilyStatus" in ks.code_of(TF) else ""
need("s_count" in status and "s_half" in status and "s_gen" in status,
     f"src/{TF}: tagFamilyStatus() does not give the tags, the half and the generation")

# Only web_doc.cpp spells the route (the release run looks for /api/sim/ texts).
for path in sorted(ks.src.rglob("*")):
    if path.suffix in (".h", ".cpp") and path.relative_to(ks.src).as_posix() != DOC:
        if PATH in path.read_text(encoding="utf-8", errors="replace"):
            problems.append(f"src/{path.relative_to(ks.src).as_posix()}: spells {PATH}")

assert not problems, "\n".join(problems)
print(f"ok: {PATH} sets, forgets and shows a made-up tag, acceptance build only, behind the gate")
