"""Read the source tree without a compiler and check that the release notes of the web pages are asked from GitHub with one more try when the connection breaks off, and that the version check is not (Spec 5.3 U3 o05, Niko11111 91c47a0, parts taken).

HTTPClient::getString() swallows the error of a body cut off midway and hands back what came so far, which then failed as "JSON: IncompleteInput" with no word of the network. The notes now read through writeToStream(), log the URL, the answer and the bytes received, and try once more after 500 ms on a transport error, not on an answer of GitHub (a 404 is the ordinary answer for a tag that was never published).

githubLatestTag() reads the version file of this fork, about a kilobyte with a length, straight off the stream. "Suchen" on the scale runs it on the loop: a second try would hold the screen up to eight seconds longer, and the daily check comes round again anyway. So it asks once."""
import re

import klipper_src as ks

ks.assert_ci_step("test_github_notes_retry.py")

REL = "services/github_release.cpp"
problems = []


def need(ok, text):
    if not ok:
        problems.append(f"src/{REL}: {text}")


def body(name, strings=False):
    try:
        return ks.body_of(REL, name, strings=strings)
    except AssertionError as e:
        problems.append(str(e))
        return ""


literals = ks.literals_of(REL)
code = ks.code_of(REL)

# The constants of the retry.
need(re.search(r"#\s*define\s+GH_API_ATTEMPTS\s+2\b", ks.source_of(REL).text), "GH_API_ATTEMPTS is not 2")
need(re.search(r"#\s*define\s+GH_API_RETRY_MS\s+500\b", ks.source_of(REL).text), "GH_API_RETRY_MS is not 500")

# One attempt: the body through writeToStream(), a cut-off body in the log
# with the bytes received.
attempt = ks.squeeze(body("ghApiAttempt"))
need("writeToStream(" in attempt, "ghApiAttempt() does not read the body through writeToStream()")
need("getString(" not in attempt, "ghApiAttempt() reads the body through getString(), which drops a cut-off")
cut = [lit for lit in literals if "cut off" in lit and "%u bytes" in lit]
need(cut and "%s" in cut[0], 'no log line "... body cut off after %u bytes" with the URL')

# The retry: up to GH_API_ATTEMPTS, a pause before the second, and only
# a transport error (a negative code) is tried again.
retry = ks.squeeze(body("ghApiGet"))
need("GH_API_ATTEMPTS" in retry and "delay(GH_API_RETRY_MS)" in retry,
     "ghApiGet() does not try up to GH_API_ATTEMPTS times with GH_API_RETRY_MS between")
need("ghApiAttempt(" in retry, "ghApiGet() does not go through ghApiAttempt()")
need("if(code>=0)break;" in retry, "ghApiGet() tries again after an answer of GitHub, not only after a transport error")

# The release notes go through it, the version check does not.
notes = ks.squeeze(body("githubReleaseByTag"))
need("ghApiGet(" in notes, "githubReleaseByTag() does not ask through ghApiGet()")
need("getString(" not in notes, "githubReleaseByTag() still reads the body through getString()")
latest = ks.squeeze(body("githubLatestTag"))
need("ghApiGet(" not in latest and "ghApiAttempt(" not in latest,
     "githubLatestTag() tries the version file twice: on the loop that holds the screen")

assert not problems, "\n".join(problems)
print("ok: release notes retried once on a transport error, version check asked once")
