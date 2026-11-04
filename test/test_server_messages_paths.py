"""Read the source tree without a compiler and check where the server's "no" is
heard (Spec 5.3 U3, v0.8.7-beta.2: the original's refusal and weight messages,
taken over with Niko's commits, in this fork's tree).

- Every way the scale writes a remaining weight ends in patchSpoolmanWeight(),
  which tells the loop when the weight did not arrive: "Gewicht nicht
  gespeichert" holds for the weight popup, auto send, full spool, the label
  raise and the AMS window alike, and a write added later beside it would not.
- serverReachNote(), the one place every flow hands its HTTP code to, passes it
  on to the refusal rules.
- A Klipper printer's Moonraker is not the scale's server: a 401 from it must
  never open "Zugang abgelehnt" for the backend's key, so its codes stay out of
  serverReachNote() (the fork's own Klipper paths, Spec 5.3 Z).
- Spoolman's access is set in one place, which tells a new key from the old one
  (the original's o08): a second way in would change the key without letting go
  of what the old one read."""
import re

import klipper_src as ks

ks.assert_ci_step("test_server_messages_paths.py")

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


def callers(name, skip):
    """Files under src/ that call name( outside the files in skip."""
    hits = []
    pat = re.compile(r"(?<![\w.>])" + re.escape(name) + r"\s*\(")
    for path in sorted(ks.src.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        rel = path.relative_to(ks.src).as_posix()
        if rel in skip:
            continue
        if pat.search(ks.code_of(rel)):
            hits.append(rel)
    return hits


# 1. One door for the remaining weight, and it reports a failure.
ACT = "services/spoolman_actions.cpp"
others = callers("backendPatchSpoolRemaining", {"services/backend_api.cpp", "services/backend_api.h", ACT})
need(not others, "backendPatchSpoolRemaining() is called outside patchSpoolmanWeight(): " + ", ".join(others))
patch = ks.squeeze(ks.body_of(ACT, "patchSpoolmanWeight"))
need("backendPatchSpoolRemaining(" in patch, f"src/{ACT}: patchSpoolmanWeight() does not write through backendPatchSpoolRemaining()")
need(ks.squeeze(ks.code_of(ACT)).count("backendPatchSpoolRemaining(") == 1,
     f"src/{ACT}: backendPatchSpoolRemaining() is called more than once")
need("weight_write_problem_pending=weightWriteProblemOf(" in patch,
     f"src/{ACT}: patchSpoolmanWeight() does not tell the loop that the weight did not arrive")

# 2. The refusal rules hear every code serverReachNote() hears.
REACH = "services/server_reach.cpp"
reach = ks.squeeze(ks.body_of(REACH, "serverReachNote"))
need("serverRefusalNote(code,user_action)" in reach,
     f"src/{REACH}: serverReachNote() does not pass the code to serverRefusalNote()")
refusal_callers = callers("serverRefusalNote", {REACH, "services/server_refusal.cpp", "services/server_refusal.h"})
need(not refusal_callers, "serverRefusalNote() is called outside serverReachNote(): " + ", ".join(refusal_callers))

# 3. Moonraker's codes stay out of serverReachNote(). In a file that knows
# Klipper, a call that hands on a code read elsewhere (a variable, not a
# backend call) sits behind a Klipper switch in the same statement.
for path in sorted(ks.src.rglob("*.cpp")):
    rel = path.relative_to(ks.src).as_posix()
    if rel == REACH:
        continue
    code = ks.code_of(rel)
    if not re.search(r"klipper|moonraker", code, re.I):
        continue
    for at, args in ks.calls(code, "serverReachNote"):
        first = ks.squeeze(args[0]) if args else ""
        if re.match(r"(backend|spoolTypes)\w*\(", first):
            continue
        before = code[max(0, code.rfind(";", 0, at)):at]
        need(re.search(r"klipper", before, re.I),
             f"src/{rel}: serverReachNote({first}, ...) hands on a code that may be Moonraker's, "
             "without a Klipper switch before it")

# 4. Spoolman's access is set through backendApplyAuth() only.
auth = callers("spoolmanSetAuth", {"services/backend.cpp", "services/backend.h", "app/backend_switch.cpp"})
need(not auth, "spoolmanSetAuth() is called outside backendApplyAuth(): " + ", ".join(auth))
SW = "app/backend_switch.cpp"
need("spoolmanSetAuth(" in ks.squeeze(ks.body_of(SW, "backendApplyAuth")) if "backendApplyAuth" in ks.code_of(SW) else False,
     f"src/{SW}: backendApplyAuth() does not set the access")
need(ks.squeeze(ks.code_of(SW)).count("spoolmanSetAuth(") == 1,
     f"src/{SW}: spoolmanSetAuth() is called outside backendApplyAuth()")

assert not problems, "\n".join(problems)
print("ok: every weight write reports, refusals heard in one place, Moonraker kept out, one way to set Spoolman's access")
