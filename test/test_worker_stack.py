"""Read the source tree without a compiler and check where the stacks of the web job and of the daily update check come from, and what a refused start tells the browser.

The two workers take their stack from PSRAM where the board has it, so the
internal heap they need before they start drops by exactly the stack
(workerStartFloor() in backend_job_rules.h). On the Waveshare 7B, with 46 to
50 kB free in operation, the GitHub check of the firmware page answered "busy"
and the daily check was skipped for a floor of 50 kB (31.10.2026). A stack in
PSRAM must never be in use while the cache is off, which a flash access does
on a board that runs its code from flash (ESP-IDF 5.5,
components/spi_flash/cache_utils.c, the assert in
spi_flash_disable_interrupts_caches_and_other_cpu()): the colour table job
reads and writes the flash on its own task (bambu_catalog.cpp), so it keeps
its stack inside. A task created with a stack from heap_caps is deleted from
the loop with vTaskDeleteWithCaps(): deleting itself, it would make a helper
task from the internal heap and abort when that fails
(components/freertos/esp_additions/idf_additions.c). A start refused for the
heap says so, to the browser and in the log, instead of "busy".

The seven workers that talk to a backend do the same since v0.8.6-beta.13
(Spec 5.3 T, V56; Punkteliste n45): the inventory list, the TLS warm-up, the
label server, the label dates, the AMS weights, the drying batch and the tag
probe. Their call paths were looked through for flash access on 31.10.2026
(docs of the meta repository, recherche/2026-10-31-beta13); "Neu aus
Datenbank" (filament_db.cpp) reads and writes the flash on its task through
filament_db_store.cpp and keeps its stack inside. The loop deletes every
parked worker in one place, workerTasksReap(), so that a stack in PSRAM is
not left counted against the others (workerFreeHeap()) after its task is
done. The spool list, the net probe, the Klipper test and the backend test
say "lowmem" like the firmware page."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

errors = []


def read(rel):
    path = src / rel
    if not path.is_file():
        errors.append(f"src/{rel}: file is missing")
        return None
    return path.read_text(encoding="utf-8", errors="replace")


def code_with_strings(text):
    """The text with its comments left out and its string literals kept."""
    return cc.Source(text).code_str


def function_body(code, signature):
    """The text between the braces of the function that starts with signature."""
    start = code.find(signature)
    if start < 0:
        return None
    depth, begin = 0, None
    for index in range(start, len(code)):
        char = code[index]
        if char == "{":
            if depth == 0:
                begin = index + 1
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return code[begin:index]
    return None


SELF_DELETE = re.compile(r"\bvTaskDelete(?:WithCaps)?\(\s*(?:NULL|nullptr)\s*\)")
PLAIN_CREATE = "xTaskCreatePinnedToCore("

# 1. The unit that creates, parks and deletes such a task.
UNIT = "services/worker_task.cpp"
unit_text = read(UNIT)
if unit_text is not None:
    unit = code_with_strings(unit_text)
    for needle, why in (
        ("xTaskCreatePinnedToCoreWithCaps(", "the stack is taken with heap_caps, PSRAM or internal"),
        ("MALLOC_CAP_SPIRAM", "the stack goes to PSRAM where it may"),
        ("MALLOC_CAP_INTERNAL", "and falls back to the internal heap"),
        ("CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM",
         "PSRAM is only tried where FreeRTOS accepts an external stack (heap_idf.c)"),
        ("vTaskDeleteWithCaps(", "the stack and the TCB go back to the heaps they came from"),
    ):
        if needle not in unit:
            errors.append(f"src/{UNIT}: does not use {needle} ({why})")
    if SELF_DELETE.search(unit):
        errors.append(f"src/{UNIT}: a task deletes itself; it must be deleted from the loop "
                      "(vTaskDeleteWithCaps(NULL) makes a helper task and aborts when it cannot)")
    if PLAIN_CREATE in unit:
        errors.append(f"src/{UNIT}: creates a task with {PLAIN_CREATE}, whose stack is always internal")
    park = function_body(unit, "void workerTaskPark(")
    if park is None or not re.search(r"vTaskSuspend\(\s*(?:NULL|nullptr)\s*\)", park):
        errors.append(f"src/{UNIT}: workerTaskPark() does not suspend the calling task")
    reap = function_body(unit, "bool workerTaskReap(")
    if reap is None or "vTaskDeleteWithCaps(" not in reap:
        errors.append(f"src/{UNIT}: workerTaskReap() does not delete the parked task")
    # Every worker that started is known to the unit, and the loop deletes
    # the parked ones in one place: a worker whose result nobody collects
    # must not keep its stack, and with it a share of workerFreeHeap().
    reap_done = function_body(unit, "bool workerTaskReapDone(")
    if reap_done is None or "workerTaskReap(" not in reap_done or "vTaskDelay(" not in reap_done:
        errors.append(f"src/{UNIT}: workerTaskReapDone() does not wait, bounded, for a task about to park")
    reap_all = function_body(unit, "void workerTasksReap(")
    if reap_all is None or "workerTaskReap(" not in reap_all:
        errors.append(f"src/{UNIT}: workerTasksReap() does not delete the parked workers")
    start_body = function_body(unit, "bool workerTaskStart(")
    if start_body is None or "workerTaskRemember(" not in start_body:
        errors.append(f"src/{UNIT}: workerTaskStart() does not remember the worker for workerTasksReap()")
loop_text = read("app/app_loop.cpp")
if loop_text is not None and "workerTasksReap()" not in code_with_strings(loop_text):
    errors.append("src/app/app_loop.cpp: the loop does not call workerTasksReap()")

# 2. The web job and the daily check create their task through the unit, end
# it by parking it, and have the loop delete it: in the tick, and before a
# start, so that a start never meets the last task still there. The floor and
# the choice of stack come from the rules, not from arithmetic of their own.
USERS = {
    "web/web_jobs.cpp": {"task": "static void webJobTask(", "tick": "void webJobsTick(",
                         "start": "bool webJobStart(", "stack": "WEB_JOB_STACK_BYTES"},
    "services/update_check.cpp": {"task": "void updateCheckTask(", "tick": "void updateCheckTick(",
                                  "start": "void maybeStartCheck(", "stack": "TASK_STACK_BYTES"},
}
# The workers that talk to a backend (V56). They have no tick of their own
# that runs on every pass; workerTasksReap() in the loop deletes them. A
# start comes only after the module has seen its task's result, so the task
# is at its last stores before parking: the start waits for that moment
# (workerTaskReapDone) instead of failing. Failing there lost the AMS grams
# that were owed (ams_view.cpp startWeights(), review of 31.10.2026).
BACKEND_USERS = {
    "services/backend_job.cpp": {"task": "static void backendJobTask(", "start": "bool backendJobStartList(",
                                 "stack": "BACKEND_JOB_STACK_BYTES", "reap": "workerTaskReapDone("},
    "services/backend_http.cpp": {"task": "static void warmTask(", "start": "static void warmStart(",
                                  "stack": "WARM_STACK_BYTES", "reap": "workerTaskReapDone("},
    "app/label_server.cpp": {"task": "static void jobTask(", "start": "static bool startJob(",
                             "stack": "LS_STACK_BYTES", "reap": "workerTaskReapDone("},
    "app/label_spool.cpp": {"task": "static void dateTask(", "start": "static bool startDateJob(",
                            "stack": "LABEL_DATE_STACK_BYTES", "reap": "workerTaskReapDone("},
    "services/ams_weights.cpp": {"task": "static void amsWeightsTask(", "start": "bool amsWeightsStart(",
                                 "stack": "AMS_WEIGHTS_STACK_BYTES", "reap": "workerTaskReapDone("},
    "services/dried_batch.cpp": {"task": "static void driedBatchTask(", "start": "bool driedBatchStart(",
                                 "stack": "DRIED_BATCH_STACK_BYTES", "reap": "workerTaskReapDone("},
    "services/tag_probe_job.cpp": {"task": "static void tagProbeTask(", "start": "bool tagProbeStart(",
                                   "stack": "TAG_PROBE_STACK_BYTES", "reap": "workerTaskReapDone("},
}
USERS.update(BACKEND_USERS)
for rel, parts in USERS.items():
    text = read(rel)
    if text is None:
        continue
    code = code_with_strings(text)
    if PLAIN_CREATE in code:
        errors.append(f"src/{rel}: creates its task with {PLAIN_CREATE}; workerTaskStart() decides the stack")
    if SELF_DELETE.search(code):
        errors.append(f"src/{rel}: its task deletes itself; it parks (workerTaskPark) and the loop deletes it")
    task = function_body(code, parts["task"])
    if task is None or "workerTaskPark(" not in task:
        errors.append(f"src/{rel}: {parts['task']}) does not end with workerTaskPark(")
    if "tick" in parts:
        tick = function_body(code, parts["tick"])
        if tick is None or "workerTaskReap(" not in tick:
            errors.append(f"src/{rel}: {parts['tick']}) does not delete a parked task (workerTaskReap)")
    start = function_body(code, parts["start"])
    if start is None:
        errors.append(f"src/{rel}: {parts['start']}) is not there")
    else:
        for needle in (parts.get("reap", "workerTaskReap("), "workerStackPlan(", "workerTaskStart("):
            if needle not in start:
                errors.append(f"src/{rel}: {parts['start']}) does not call {needle}")
        if re.search(r"-\s*" + re.escape(parts["stack"]) + r"\b", start):
            errors.append(f"src/{rel}: {parts['start']}) takes the stack off the floor itself; "
                          "workerStackPlan() and workerStartFloor() do that")

# 3. Which web jobs may move their stack is decided in one place, and the
# colour table is the one that may not: it reads and writes the flash on the
# job's task. Every other kind was looked through for flash access on
# 31.10.2026 (the request paths of the host test, the spool list, the GitHub
# check and notes, the net probe and the Klipper test); a new kind is looked
# through and then left out of this list, or put in it.
WEB_JOBS = "web/web_jobs.cpp"
web_text = read(WEB_JOBS)
if web_text is not None:
    web = code_with_strings(web_text)
    helper = "static bool webJobStackMayGoOutside("
    if web.count(helper) != 1:
        errors.append(f"src/{WEB_JOBS}: does not define {helper} exactly once")
    body = function_body(web, helper)
    if body is not None:
        kinds = set(re.findall(r"\bWJ_[A-Z_]+\b", body))
        if kinds != {"WJ_BAMBU_CATALOG"}:
            errors.append(f"src/{WEB_JOBS}: webJobStackMayGoOutside() names {sorted(kinds)}, "
                          "wants only WJ_BAMBU_CATALOG, the one kind that touches the flash")
        if "return false" not in body and "!=" not in body:
            errors.append(f"src/{WEB_JOBS}: webJobStackMayGoOutside() does not keep the colour table inside")
    start = function_body(web, "bool webJobStart(")
    if start is not None and "webJobStackMayGoOutside(" not in start:
        errors.append(f"src/{WEB_JOBS}: webJobStart() does not ask webJobStackMayGoOutside(")
    # A start refused for the heap is logged, through the gate it already has.
    if not re.search(r'logSDf\(\s*"Web job: [^"]*postponed, heap %u < %u', web):
        errors.append(f'src/{WEB_JOBS}: a start refused for the heap is not logged ("Web job: ... postponed, heap %u < %u")')
    if "WJR_LOW_HEAP" not in web:
        errors.append(f"src/{WEB_JOBS}: a start refused for the heap is not told apart (WJR_LOW_HEAP)")

# 3b. The backend workers may move their stack: none of them touches the flash
# on its own task (looked through on 31.10.2026, see the docstring). "Neu aus
# Datenbank" does, through the store of the downloaded database, so it never
# asks for PSRAM. If the store stops writing the flash itself, look again.
for rel, parts in BACKEND_USERS.items():
    text = read(rel)
    if text is None:
        continue
    start = function_body(code_with_strings(text), parts["start"])
    if start is not None and "workerStackCanGoOutside(" not in start:
        errors.append(f"src/{rel}: {parts['start']}) never tries a stack in PSRAM (workerStackCanGoOutside)")
FDB = "services/filament_db.cpp"
fdb_text = read(FDB)
if fdb_text is not None:
    fdb = code_with_strings(fdb_text)
    if "workerStackCanGoOutside(" in fdb or "MALLOC_CAP_SPIRAM" in fdb and "xTaskCreatePinnedToCoreWithCaps(" in fdb:
        errors.append(f"src/{FDB}: the database job asks for a stack in PSRAM, but it reads and writes "
                      "the flash on its own task (filament_db_store.cpp)")
store_text = read("services/filament_db_store.cpp")
if store_text is not None and "esp_partition_write(" not in code_with_strings(store_text):
    errors.append("src/services/filament_db_store.cpp: no longer writes the flash itself; "
                  "check whether the database job may move its stack too")

# 3a. Every worker that tests a floor tests it against workerFreeHeap(): the
# internal heap less the stacks in PSRAM of the workers still there. A stack
# moved to PSRAM no longer lowers ESP.getFreeHeap(), so beside a GitHub check
# a worker on the internal heap would see 14-16 kB more than before and start
# where its floor used to keep it out (review of 31.10.2026: the tag probe
# beside the daily check on the 7B). Counted like this, every other worker
# sees the heap as it did with the stack inside.
FLOOR_READERS = [
    "services/backend_job.cpp", "app/label_server.cpp", "app/label_spool.cpp",
    "services/ams_weights.cpp", "services/dried_batch.cpp", "services/filament_db.cpp",
    "services/tag_probe_job.cpp", "services/backend_http.cpp", "web/web_jobs.cpp",
    "services/update_check.cpp",
]
RAW_FLOOR_READ = re.compile(r"=\s*\(uint32_t\)\s*ESP\.getFreeHeap\(\)\s*;|ESP\.getFreeHeap\(\)\s*<")
for rel in FLOOR_READERS:
    text = read(rel)
    if text is None:
        continue
    code = code_with_strings(text)
    if "workerFreeHeap()" not in code:
        errors.append(f"src/{rel}: does not test its floor against workerFreeHeap()")
    if RAW_FLOOR_READ.search(code):
        errors.append(f"src/{rel}: tests a floor against ESP.getFreeHeap(), which misses the stacks in PSRAM")
if unit_text is not None:
    unit = code_with_strings(unit_text)
    body = function_body(unit, "uint32_t workerFreeHeap(")
    if body is None or "MALLOC_CAP_INTERNAL" not in body:
        errors.append(f"src/{UNIT}: workerFreeHeap() does not start from the internal heap")

catalog_text = read("bambu/bambu_catalog.cpp")
if catalog_text is not None and "esp_partition_write(" not in code_with_strings(catalog_text):
    errors.append("src/bambu/bambu_catalog.cpp: no longer writes the flash itself; "
                  "check whether the colour table job may move its stack too")

header = read("web/web_jobs.h")
if header is not None and not re.search(r"\bWebJobRefusal\s+webJobLastRefusal\(\s*\)", header):
    errors.append("src/web/web_jobs.h: does not declare WebJobRefusal webJobLastRefusal()")

# 4. What the browser hears. The GitHub check and the release notes answer
# "lowmem" when the start was refused for the heap, and the page has a text
# of its own for it; so has the colour table on the tags page.
LOWMEM_REPLY = re.compile(r"webJobLastRefusal\(\)\s*==\s*WJR_LOW_HEAP")
PAGES = {
    "web/pages/page_firmware.cpp": {"handlers": ['srv.on("/api/ota/check"', 'srv.on("/api/ota/notes"'],
                                    "js": r"d\.error\s*===\s*'lowmem'\s*\?\s*G\.lowmem",
                                    "string": "STR_W_FW_LOWMEM"},
    "web/web_bambu_catalog.cpp": {"handlers": ['srv.on("/api/bambu/catalog", HTTP_POST'],
                                  "js": r"d\.error\s*===\s*'lowmem'",
                                  "string": "STR_W_BCAT_LOWMEM"},
    # The spool list of the tags page shows the error in its picker.
    "web/pages/page_tags.cpp": {"handlers": ['srv.on("/api/spools", HTTP_GET'],
                                "js": r"d\.error\s*===\s*'lowmem'\s*\?\s*M\.lowmem",
                                "string": "STR_W_LOWMEM"},
    # A JSON answer for whoever asks; no page of its own.
    "web/web_net_probe.cpp": {"handlers": ['srv.on("/api/net/probe"'], "js": None, "string": None},
    # A test the save could not start is asked for again by the page.
    "web/pages/page_klipper.cpp": {"handlers": ['srv.on("/api/klipper/test", HTTP_POST'],
                                   "js": r"\.lowmem\s*\?\s*K\.lowmem",
                                   "string": "STR_W_LOWMEM"},
    # The backend test after a new address: the page shows the answer's msg.
    "web/pages/page_backend.cpp": {"handlers": ['srv.on("/api/host", HTTP_POST'], "js": None,
                                   "string": "STR_W_HOST_SAVED_LOWMEM"},
}
for rel, page in PAGES.items():
    text = read(rel)
    if text is None:
        continue
    code = code_with_strings(text)
    for handler in page["handlers"]:
        at = code.find(handler)
        body = code[at:code.find("});", at)] if at >= 0 else ""
        if not LOWMEM_REPLY.search(body) or "lowmem" not in body:
            errors.append(f"src/{rel}: {handler} does not answer \"lowmem\" for a start refused for the heap")
    if page["js"] and not re.search(page["js"], code):
        errors.append(f"src/{rel}: the page does not show a text of its own for \"lowmem\"")
    if page["string"] and page["string"] not in code:
        errors.append(f"src/{rel}: does not use {page['string']}")

lang_h = read("lang.h")
lang_cpp = read("lang.cpp")
for name in ("STR_W_FW_LOWMEM", "STR_W_BCAT_LOWMEM", "STR_W_LOWMEM", "STR_W_HOST_SAVED_LOWMEM"):
    if lang_h is not None and not re.search(r"^\s*" + name + r",", lang_h, re.M):
        errors.append(f"src/lang.h: {name} is missing")
    if lang_cpp is not None and f"// {name}" not in lang_cpp:
        errors.append(f"src/lang.cpp: the row of {name} is missing")

if errors:
    print("worker stack: FAILED")
    for error in errors:
        print("  " + error)
    sys.exit(1)
print("worker stack: ok")
