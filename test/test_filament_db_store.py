"""Read the source tree without a compiler and check that "Neu aus Datenbank" downloads on a task whose stack may lie in PSRAM, while the stored index is read by the loop and written by a step of its own, with its stack in internal RAM and no network (Spec 5.3 T, V64; Punkteliste n64).

The three loads of the picker (makers, a maker's materials, a material's entries) ran on a task with a 16 kB stack from the internal heap, because the index load read the stored index and wrote a new one on that task, and a task whose stack lies in PSRAM must not touch the flash (ESP-IDF 5.5, components/spi_flash/cache_utils.c line 141, the assert in spi_flash_disable_interrupts_caches_and_other_cpu()). Its floor was the whole 50 000 B at https; the 7B has 49.4 to 50.2 kB free in operation, so against an https Spoolman the picker stayed empty and asked again every two seconds. And the stored index was read in one go into PSRAM, which takes a bounce buffer of up to 16 kB from the internal heap (ESP-IDF 5.5 esp_flash_api.c esp_flash_read(), MAX_READ_CHUNK; the colour table went down to 24 128 B that way in the acceptance of beta.19).

Now the loop reads the stored index in small pieces through its own stack before the task starts, the task only downloads and hands a new index over, and a small task with an internal stack writes it, started by the loop with the reserve a background request leaves (26 000 B, Spec 8 T) and its own stack free, the way the colour table does since V59 (test_bambu_catalog_store.py)."""
import re

import klipper_src as ks

FDB = "services/filament_db.cpp"
STORE = "services/filament_db_store.cpp"
STORE_H = "services/filament_db_store.h"
FLASH = re.compile(r"\besp_partition_\w+\(|\bnvs_\w+\(|\bprefs\w*\(|\bUpdate\.")

ks.assert_ci_step("test_filament_db_store.py")

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


def body(rel, name, strings=False):
    try:
        return ks.body_of(rel, name, strings=strings)
    except AssertionError as e:
        problems.append(str(e))
        return ""


# 1. What runs on the database task touches no flash: the load puts the index
# the loop read in place, the save hands a copy over.
for name in ("fdbStoreLoad", "fdbStoreSave"):
    b = body(STORE, name)
    hit = FLASH.search(b)
    need(not hit, f"src/{STORE}: {name}() runs on the database task and touches the flash ({hit.group(0) if hit else ''})")
save = ks.squeeze(body(STORE, "fdbStoreSave"))
need("MALLOC_CAP_SPIRAM" in save, f"src/{STORE}: fdbStoreSave() does not build the copy in PSRAM")
need("__sync_synchronize()" in save, f"src/{STORE}: fdbStoreSave() hands the copy over without a barrier before the flag")

# 2. The stored index is read by the loop, before the task starts, and only
# for the index load.
prep = ks.squeeze(body(STORE, "fdbStorePrepare"))
need("esp_partition_read(" in prep or "readChunked(" in prep,
     f"src/{STORE}: fdbStorePrepare() does not read the stored index")
start = ks.squeeze(body(FDB, "startTask"))
i_prep = start.find("fdbStorePrepare(")
i_task = start.find("workerTaskStart(")
need(i_prep >= 0, f"src/{FDB}: startTask() does not have the loop read the stored index (fdbStorePrepare())")
need(i_prep < 0 or "job==FDB_JOB_INDEX" in start[max(0, i_prep - 60):i_prep],
     f"src/{FDB}: fdbStorePrepare() is not called for the index load only")
need(i_prep < 0 or i_task < 0 or i_prep < i_task,
     f"src/{FDB}: startTask() starts the task before the stored index is read")
# After the floor: a refused start must not read the flash on every retry.
i_floor = start.find("plan.outside")
need(i_prep < 0 or i_floor < 0 or i_floor < i_prep,
     f"src/{FDB}: startTask() reads the stored index before the floor is known to let the task start")

# 3. Every flash read of the store goes into memory on the stack: the header
# or a small chunk, never PSRAM.
text = ks.source_of(STORE).code_str
reads = ks.calls(text, "esp_partition_read")
need(len(reads) > 0, f"src/{STORE}: reads no flash at all")
for _, args in reads:
    if len(args) != 4:
        continue
    need(args[2].strip() in ("&h", "chunk"),
         f"src/{STORE}: esp_partition_read(..., {args[2].strip()}, ...) reads into memory that may lie in PSRAM; "
         "read into the header on the stack or a chunk on the stack")
chunk = re.search(r"\buint8_t\s+chunk\[(\d+)\]\s*;", text)
need(chunk is not None and int(chunk.group(1)) <= 512,
     f"src/{STORE}: no chunk buffer of at most 512 B on the stack for the flash reads")

# 4. The write: on a task of its own, its stack inside, started by the loop
# with the reserve of a background request and its stack free.
write = ks.squeeze(body(STORE, "writeStore"))
for needle in ("esp_partition_erase_range(", "esp_partition_write("):
    need(needle in write, f"src/{STORE}: writeStore() lacks {needle}")
task = ks.squeeze(body(STORE, "fdbStoreTask"))
need("writeStore(" in task, f"src/{STORE}: fdbStoreTask() does not write the index")
need("workerTaskPark(" in task, f"src/{STORE}: fdbStoreTask() does not park itself at the end")
need("uxTaskGetStackHighWaterMark(" in task, f"src/{STORE}: fdbStoreTask() does not log what its stack had left")
tick = ks.squeeze(body(STORE, "fdbStoreTick"))
for needle in ("workerTaskStart(", "workerFreeHeap()", "FDB_STORE_MIN_HEAP", "FDB_STORE_STACK_BYTES",
               "logGateDue(", "FDB_STORE_START_WAIT_MS"):
    need(needle in tick, f"src/{STORE}: fdbStoreTick() lacks {needle}")
need(re.search(r"WorkerStackPlan\w*=\{false,", tick) is not None,
     f"src/{STORE}: the store task may get a stack in PSRAM (its plan must say outside=false)")
raw = (ks.src / STORE).read_text(encoding="utf-8")
stack = re.search(r"#define\s+FDB_STORE_STACK_BYTES\s+(\d+)\b", raw)
need(stack is not None and 4096 <= int(stack.group(1)) <= 8192,
     f"src/{STORE}: FDB_STORE_STACK_BYTES is not a stack of 4 to 8 kB")
need(re.search(r"#define\s+FDB_STORE_RESERVE_BYTES\s+26000UL\b", raw) is not None,
     f"src/{STORE}: FDB_STORE_RESERVE_BYTES is not the 26 000 B a background request leaves (Spec 8 T)")
need(re.search(r"#define\s+FDB_STORE_MIN_HEAP\s+\(\s*FDB_STORE_RESERVE_BYTES\s*\+\s*FDB_STORE_STACK_BYTES\s*\)", raw)
     is not None, f"src/{STORE}: FDB_STORE_MIN_HEAP is not the reserve plus the store's stack")
need("fdbStoreTick();" in ks.squeeze(ks.code_of("app/app_loop.cpp")),
     "src/app/app_loop.cpp: the loop does not call fdbStoreTick()")
header = (ks.src / STORE_H).read_text(encoding="utf-8")
for decl in (r"bool\s+fdbStorePrepare\(\s*const\s+char\s*\*\s*\w+\s*\)", r"void\s+fdbStoreTick\(\s*\)"):
    need(re.search(decl, header) is not None, f"src/{STORE_H}: does not declare {decl}")

# 5. The task: its stack may go to PSRAM, and its line says where it lay, the
# heap it started with and how low it went (V64, Spec 8 T).
task_fdb = ks.squeeze(body(FDB, "fdbTask", strings=True))
need("heapLowBegin()" in task_fdb and "heapLowEnd()" in task_fdb,
     f"src/{FDB}: fdbTask() does not measure how low the internal heap went (heapLowBegin/End)")
need('s_worker.outside?"PSRAM":"internal"' in task_fdb,
     f"src/{FDB}: fdbTask()'s line does not say where its stack lay")
need("workerStackCanGoOutside(" in start, f"src/{FDB}: startTask() never tries a stack in PSRAM")

# 5a. A line written on a task goes through the queue of sd_logger.cpp, 160
# bytes with its terminator (SD_QUEUE_LINE_MAX): a longer one is cut. The
# task's line lost "stack in PSRAM" to that on the 7B (acceptance of beta.21,
# round 1). Every line of the tasks fits with the widest numbers.
WIDTH = {"u": 10, "lu": 10, "d": 11, "s": 12}
logger = (ks.src / "hardware/sd_logger.cpp").read_text(encoding="utf-8")
qmax = re.search(r"#define\s+SD_QUEUE_LINE_MAX\s+(\d+)", logger)
need(qmax is not None, "src/hardware/sd_logger.cpp: SD_QUEUE_LINE_MAX is not there")
limit = int(qmax.group(1)) - 1 if qmax else 159
for rel, name in ((FDB, "fdbTask"), (STORE, "fdbStoreLoad"), (STORE, "fdbStoreSave"), (STORE, "fdbStoreTask")):
    b = body(rel, name, strings=True)
    for _, args in ks.calls(b, "logSDf"):
        fmt = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', args[0]))
        width = len(re.sub(r"%(lu|u|d|s)", "", fmt)) + sum(WIDTH[c] for c in re.findall(r"%(lu|u|d|s)", fmt))
        need(width <= limit, f"src/{rel}: a line of {name}() runs to {width} characters, the queue keeps {limit}: {fmt[:60]}...")

# 6. The acceptance firmware passes the stored index over once, so the store
# can be shown without waiting a week (Spec 5.3 AA, V64).
doc = (ks.src / "web/web_doc.cpp").read_text(encoding="utf-8")
route = re.search(r'srv\.on\("/api/sim/fdb-forget",\s*HTTP_POST', doc)
need(route is not None, "src/web/web_doc.cpp: no POST /api/sim/fdb-forget")
if route:
    tail = doc[route.end():route.end() + 600]
    need("webRequire(srv, GATE_MAINT" in tail, "src/web/web_doc.cpp: /api/sim/fdb-forget is not behind the maintenance gate")
    need("fdbForgetStoredIndexOnce(" in tail, "src/web/web_doc.cpp: /api/sim/fdb-forget does not call fdbForgetStoredIndexOnce()")
    need("logSD" in tail, "src/web/web_doc.cpp: /api/sim/fdb-forget does not log what it did")
need(doc.find("/api/sim/fdb-forget") > doc.find("#if defined(SSC_ACCEPTANCE)"),
     "src/web/web_doc.cpp: /api/sim/fdb-forget is not inside the acceptance build")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_filament_db_store: ok")
