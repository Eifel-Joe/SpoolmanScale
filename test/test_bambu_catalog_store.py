"""Read the source tree without a compiler and check that BambuStudio's colour table is downloaded by a job whose stack may lie in PSRAM and written to flash by a step of its own, with its stack in internal RAM and no network (Spec 5.3 T, V59; Punkteliste n50).

The daily check of the table needed 50 000 B of free internal heap: its job read the stored copy's ETag and wrote a changed table on its own task, and a task whose stack lies in PSRAM must not touch the flash (ESP-IDF 5.5, components/spi_flash/cache_utils.c line 141, the assert in spi_flash_disable_interrupts_caches_and_other_cpu()). The 7B has 49.4 to 50.2 kB free in operation; on 31.10./01.11.2026 the check was refused eleven times in a row.

Now the loop reads the ETag before the job starts, the job only downloads and builds the new copy in PSRAM, and a small task with an internal stack writes it while the job waits for its verdict, so the result still says whether the table was stored. The job's floor drops by its stack like the GitHub check's. The store task starts only with the reserve a background request leaves (26 000 B, Spec 8 T) plus its own stack free."""
import re

import klipper_src as ks

CAT = "bambu/bambu_catalog.cpp"
JOBS = "web/web_jobs.cpp"
FLASH = re.compile(r"\besp_partition_\w+\(|\bstoredEtag\(|\bnvs_\w+\(|\bprefs\w*\(|\bUpdate\.")

ks.assert_ci_step("test_bambu_catalog_store.py")

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


# 1. What runs on the download job touches no flash: the download, and the
# build of the new copy in PSRAM.
for name in ("bambuCatalogDownload", "catalogBuild"):
    b = body(CAT, name)
    hit = FLASH.search(b)
    need(not hit, f"src/{CAT}: {name}() runs on the download job and touches the flash ({hit.group(0) if hit else ''})")
dl = ks.squeeze(body(CAT, "bambuCatalogDownload"))
need("catalogBuild(" in dl, f"src/{CAT}: bambuCatalogDownload() does not build the copy with catalogBuild()")
need("catalogAwaitStore(" in dl,
     f"src/{CAT}: bambuCatalogDownload() does not wait for the store's verdict (catalogAwaitStore())")
need("bambuCatalogStore(" not in dl and "catalogWrite(" not in dl,
     f"src/{CAT}: bambuCatalogDownload() still writes the table itself")

# 2. The ETag of the stored copy is read on the loop, before the job starts.
prep = ks.squeeze(body(CAT, "bambuCatalogPrepareDownload"))
need("storedEtag(" in prep, f"src/{CAT}: bambuCatalogPrepareDownload() does not read the stored ETag")
start = ks.squeeze(body(JOBS, "webJobStart"))
i_prep = start.find("bambuCatalogPrepareDownload(")
i_task = start.find("workerTaskStart(")
need(i_prep >= 0, f"src/{JOBS}: webJobStart() does not hand the colour table its ETag (bambuCatalogPrepareDownload())")
need(i_prep < 0 or "kind==WJ_BAMBU_CATALOG" in start[max(0, i_prep - 60):i_prep],
     f"src/{JOBS}: bambuCatalogPrepareDownload() is not called for the colour table only")
need(i_prep < 0 or i_task < 0 or i_prep < i_task,
     f"src/{JOBS}: webJobStart() starts the task before the ETag is handed over")

# 3. The write: on a task of its own, its stack inside, started by the loop
# with the reserve of a background request and its stack free.
write = ks.squeeze(body(CAT, "catalogWrite"))
for needle in ("esp_partition_erase_range(", "esp_partition_write(", "esp_partition_read("):
    need(needle in write, f"src/{CAT}: catalogWrite() lacks {needle}")
task = ks.squeeze(body(CAT, "catalogStoreTask"))
need("catalogWrite(" in task, f"src/{CAT}: catalogStoreTask() does not write the table")
need("workerTaskPark(" in task, f"src/{CAT}: catalogStoreTask() does not park itself at the end")
tick = ks.squeeze(body(CAT, "bambuCatalogStoreTick"))
for needle in ("workerTaskStart(", "workerFreeHeap()", "CAT_STORE_MIN_HEAP", "CAT_STORE_STACK_BYTES"):
    need(needle in tick, f"src/{CAT}: bambuCatalogStoreTick() lacks {needle}")
need(re.search(r"WorkerStackPlan\w*=\{false,", tick) is not None,
     f"src/{CAT}: the store task may get a stack in PSRAM (its plan must say outside=false)")
raw = (ks.src / CAT).read_text(encoding="utf-8")
stack = re.search(r"#define\s+CAT_STORE_STACK_BYTES\s+(\d+)\b", raw)
need(stack is not None and 4096 <= int(stack.group(1)) <= 8192,
     f"src/{CAT}: CAT_STORE_STACK_BYTES is not a stack of 4 to 8 kB")
need(re.search(r"#define\s+CAT_STORE_RESERVE_BYTES\s+26000UL\b", raw) is not None,
     f"src/{CAT}: CAT_STORE_RESERVE_BYTES is not the 26 000 B a background request leaves (Spec 8 T)")
need(re.search(r"#define\s+CAT_STORE_MIN_HEAP\s+\(\s*CAT_STORE_RESERVE_BYTES\s*\+\s*CAT_STORE_STACK_BYTES\s*\)", raw)
     is not None, f"src/{CAT}: CAT_STORE_MIN_HEAP is not the reserve plus the store's stack")
need("bambuCatalogStoreTick();" in ks.squeeze(ks.code_of("app/app_loop.cpp")),
     "src/app/app_loop.cpp: the loop does not call bambuCatalogStoreTick()")

# 4. The job: its stack may go to PSRAM now, and it asks the download with the
# mode the loop prepared.
may = ks.squeeze(body(JOBS, "webJobStackMayGoOutside"))
need("WJ_BAMBU_CATALOG" not in may, f"src/{JOBS}: webJobStackMayGoOutside() still keeps the colour table inside")
run = ks.squeeze(body(JOBS, "runBambuCatalog"))
need("bambuCatalogDownload(s_res.err" in run, f"src/{JOBS}: runBambuCatalog() does not call bambuCatalogDownload(err, ...)")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_bambu_catalog_store: ok")
