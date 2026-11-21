"""Read the source tree without a compiler and check how saving power on the 7B (Spec 5.3 AD, V81, V82) is wired in: only the 7B switches anything (BOARD_SLEEP_CUTS), the held lines are let go at every start before anything else runs, a wake by the touch line without a touch sleeps on before the board is set up, the deep sleep goes through the shutdown list with the SD card last and leaves the display's converter and reset alone, the reader is skipped by the main loop and the diagnosis while it is off and switched on for the reset probe, the reader is kept on while the scale waits for a tag, every way that writes a firmware asks for the reason it is refused and says when the supply is not known yet, and nothing of the diagnosis firmware came along."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

failures = []


def expect(what, ok, detail=""):
    if not ok:
        failures.append(f"{what}" + (f" ({detail})" if detail else ""))


def text(rel):
    path = src / rel
    if not path.is_file():
        expect(f"src/{rel}: file is missing", False)
        return ""
    return path.read_text(encoding="utf-8")


def code(rel):
    """The source without comments and string contents, so that a word in a comment proves nothing."""
    return cc.Source(text(rel)).code


def body(source, signature):
    """The body of the first function whose definition starts with signature, braces counted."""
    i = source.find(signature)
    if i < 0:
        return None
    j = source.find("{", i)
    if j < 0:
        return None
    depth = 0
    for k in range(j, len(source)):
        if source[k] == "{":
            depth += 1
        elif source[k] == "}":
            depth -= 1
            if depth == 0:
                return source[j + 1:k]
    return None


def in_order(where, hay, needles):
    pos = [hay.find(n) for n in needles]
    expect(f"{where}: {', '.join(needles)} in this order", all(p >= 0 for p in pos) and pos == sorted(pos), str(pos))


# 1. Only the 7B. Its board_config switches it on; the 7 and the WT32 do not
#    know the word, so their folders stay as they were.
m = re.search(r"^#define\s+BOARD_SLEEP_CUTS\s+(\d+)\s*$", text("boards/waveshare_s3_lcd7b/board_config.h"), re.M)
expect("boards/waveshare_s3_lcd7b/board_config.h: BOARD_SLEEP_CUTS 1", m is not None and m.group(1) == "1",
       m.group(1) if m else "not defined")
for board in ("waveshare_s3_lcd7", "wt32_sc01_plus"):
    for p in (src / "boards" / board).rglob("*"):
        if p.is_file():
            t = p.read_text(encoding="utf-8", errors="replace")
            for word in ("BOARD_SLEEP_CUTS", "sleepPower", "sleep_power"):
                expect(f"src/boards/{board}/{p.name}: no {word} (only the 7B saves power this way)", word not in t)
header = text("app/sleep_power.h")
expect("app/sleep_power.h: BOARD_SLEEP_CUTS defaults to 0 where a board does not set it",
       re.search(r"#ifndef BOARD_SLEEP_CUTS\s*\n\s*#define BOARD_SLEEP_CUTS 0", header) is not None)
sp_text = text("app/sleep_power.cpp")
sp = cc.Source(sp_text).code
for fn in ("void sleepPowerEarly()", "void sleepPowerBeforeSleep()", "void sleepPowerReaderTick(",
           "bool sleepPowerReaderParked()", "void sleepPowerReaderWake()", "void sleepPowerReaderWanted()",
           "void sleepPowerReport()", "void sleepPowerArmed("):
    expect(f"app/sleep_power.cpp: {fn} is defined under BOARD_SLEEP_CUTS and as a stub without",
           sp.count(fn) == 2, str(sp.count(fn)))
expect("app/sleep_power.cpp: the real functions live under #if BOARD_SLEEP_CUTS", "#if BOARD_SLEEP_CUTS" in sp_text)

# 2. The start. sleepPowerEarly() comes first in appSetup(), before the
#    serial port, the crumbs and boardEarlyInit(), which puts the touch chip
#    in reset and so would wipe the report the wake rule asks for.
boot = code("app/app_boot.cpp")
setup = body(boot, "void appSetup()")
expect("app/app_boot.cpp: appSetup() is there", setup is not None)
if setup:
    first = setup.strip().split(";")[0].strip()
    expect("app/app_boot.cpp: sleepPowerEarly() is the first call in appSetup()", first == "sleepPowerEarly()", first)
    in_order("app/app_boot.cpp appSetup()", setup, ["sleepPowerEarly();", "boardEarlyInit();", "initSD();",
                                                     "sleepPowerReport();"])
early = body(sp, "void sleepPowerEarly()")
expect("app/sleep_power.cpp: sleepPowerEarly() is there", early is not None)
if early:
    # The held lines are let go on every start: after this firmware's own
    # sleep, after the diagnosis firmware's, after a crash. The only way past
    # it is back to sleep, which keeps them held.
    stmts = [s.strip() for s in early.strip().rstrip(";").split(";")]
    expect("app/sleep_power.cpp: sleepPowerEarly() ends with releaseHolds()", stmts[-1] == "releaseHolds()", stmts[-1])
    expect("app/sleep_power.cpp: sleepPowerEarly() has no return before releaseHolds()", "return" not in early)
    expect("app/sleep_power.cpp: sleepPowerEarly() asks the wake rule", "sleeppwr::wakeRule(" in sp)
    expect("app/sleep_power.cpp: the wake rule is asked only after a wake by the touch line (EXT0)",
           "ESP_SLEEP_WAKEUP_EXT0" in sp)
release = body(sp, "static void releaseHolds()")
expect("app/sleep_power.cpp: releaseHolds() is there", release is not None)
if release:
    for pin in ("hw_pins::PN532_RESET_WIRE", "hw_pins::SD_CS"):
        expect(f"app/sleep_power.cpp: releaseHolds() lets go of {pin}",
               re.search(rf"gpio_hold_dis\(\s*\(gpio_num_t\)\s*{re.escape(pin)}\s*\)", release) is not None)
expect("app/sleep_power.cpp: the touch chip's status register 0x814E is read", "0x81" in sp and "0x4E" in sp)
# The expander kept its pins through the sleep. The way back to sleep does
# not take it up at all: begin() writes its pins, boardEarlyInit() does that
# on a real start.
early_path = body(sp, "static void sleepOnWithoutTouch()")
expect("app/sleep_power.cpp: sleepOnWithoutTouch() is there", early_path is not None)
if early_path:
    expect("app/sleep_power.cpp: the way back to sleep leaves the expander alone", "io_ch32v003" not in early_path)
panel = code("boards/waveshare_s3_lcd7b/panel.cpp")
prep = body(panel, "void panelPrepareDeepSleep()")
expect("boards/waveshare_s3_lcd7b/panel.cpp: panelPrepareDeepSleep() is there", prep is not None)
if prep:
    in_order("boards/waveshare_s3_lcd7b/panel.cpp panelPrepareDeepSleep()", prep,
             ["sleepPowerArmed(", "esp_sleep_enable_ext0_wakeup("])

# 3. The deep sleep: after the panel's own preparation, through the shutdown
#    list, and nothing logged after it, since the card is deselected last.
dp = code("hardware/display_power.cpp")
hpm = body(dp, "void handlePowerManagement()")
expect("hardware/display_power.cpp: handlePowerManagement() is there", hpm is not None)
if hpm:
    in_order("hardware/display_power.cpp handlePowerManagement()", hpm,
             ["displayPrepareDeepSleep();", "sleepPowerBeforeSleep();", "esp_deep_sleep_start();"])
    tail = hpm[hpm.find("sleepPowerBeforeSleep();"):hpm.find("esp_deep_sleep_start();")]
    expect("hardware/display_power.cpp: nothing is logged between sleepPowerBeforeSleep() and the sleep",
           "logSD" not in tail and "Serial" not in tail, tail)
before = body(sp, "void sleepPowerBeforeSleep()")
expect("app/sleep_power.cpp: sleepPowerBeforeSleep() is there", before is not None)
if before:
    expect("app/sleep_power.cpp: sleepPowerBeforeSleep() follows sleeppwr::sleepCuts()", "sleeppwr::sleepCuts(" in before)
    expect("app/sleep_power.cpp: sleepPowerBeforeSleep() picks the reader's way by nfcResetVerified()",
           "nfcResetVerified(" in before)
for step, needle in (("STEP_READER_LINE", "gpio_hold_en"), ("STEP_READER_COMMAND", "nfcHardwarePowerDown("),
                     ("STEP_SCALE", "scaleHardwarePowerDown("), ("STEP_SD_DESELECT", "hw_pins::EXIO_SD_CS"),
                     ("STEP_RS485_HIGH", "hw_pins::SD_CS")):
    expect(f"app/sleep_power.cpp: {step} is carried out ({needle})", step in sp and needle in sp)
# V82: the display's converter and its reset stay as they are.
for word in ("EXIO_LCD_VDD", "EXIO_LCD_RST", "panelStop", "panelPowerOff", "panelDiagPowerOff"):
    expect(f"app/sleep_power.cpp: does not touch the panel ({word}, V82)", word not in sp)
if prep:
    for word in ("EXIO_LCD_VDD", "EXIO_LCD_RST"):
        expect(f"boards/waveshare_s3_lcd7b/panel.cpp: panelPrepareDeepSleep() leaves {word} alone (V82)", word not in prep)
expect("hardware/nfc.cpp: nfcHardwarePowerDown() sends the PowerDown command 0x16",
       re.search(r"bool nfcHardwarePowerDown\(\)", code("hardware/nfc.cpp")) is not None
       and re.search(r"0x16", code("hardware/nfc.cpp")) is not None)
expect("hardware/scale.cpp: scaleHardwarePowerDown() clears PUD and PUA by sleeppwr::nauPowerDown()",
       "sleeppwr::nauPowerDown(" in code("hardware/scale.cpp"))

# 4. The reader while it is off: the main loop neither polls nor revives it,
#    the diagnosis does not call it missing, the reset probe switches it on
#    first.
loop = code("app/app_loop.cpp")
expect("app/app_loop.cpp: drives the reader rule (sleepPowerReaderTick)", "sleepPowerReaderTick(" in loop)
expect("app/app_loop.cpp: the scan skips a reader that is off",
       re.search(r"if\s*\(\s*nfc_ok\s*&&\s*!sleepPowerReaderParked\(\)\s*\)", loop) is not None)
expect("app/app_loop.cpp: the revival skips a reader that is off",
       re.search(r"if\s*\(\s*!nfc_ok\s*&&\s*!sleepPowerReaderParked\(\)\s*&&", loop) is not None)
probe = loop.find("nfcResetSelfTest()")
expect("app/app_loop.cpp: the reset probe switches the reader on first (sleepPowerReaderWake)",
       probe >= 0 and "sleepPowerReaderWake();" in loop[max(0, probe - 400):probe])
expect("services/diagnostics.cpp: a reader that is off counts as on the bus",
       re.search(r"pn532_on_bus\s*=\s*sleepPowerReaderParked\(\)\s*\|\|\s*i2cPresent\(", code("services/diagnostics.cpp"))
       is not None)
# E1 a: on while the scale waits for a tag.
tick = body(loop, "static bool waitingForTag()")
expect("app/app_loop.cpp: waitingForTag() is there", tick is not None)
if tick:
    for needle in ("isTagWritePopupOpen()", "isSecondTagPopupOpen()", "tagWriteState()"):
        expect(f"app/app_loop.cpp: waitingForTag() asks {needle}", needle in tick)
expect("services/tag_write.cpp: a scan request keeps the reader on (sleepPowerReaderWanted)",
       "sleepPowerReaderWanted(" in (body(code("services/tag_write.cpp"), "void tagScanRequest()") or ""))
expect("web/pages/page_tags.cpp: the tag page's poll keeps the reader on (sleepPowerReaderWanted)",
       "sleepPowerReaderWanted(" in code("web/pages/page_tags.cpp"))

# 5. The update lock until the supply is known (Spec 5.3 AD), on all three
#    ways, each with its own text for that reason; the web gives "supply".
expect("services/battery.cpp: asks bat::updateBlock() with the time since the first reading",
       "bat::updateBlock(" in code("services/battery.cpp"))
expect("services/battery.h: batteryUpdateBlock() gives the reason",
       re.search(r"bat::Block\s+batteryUpdateBlock\(\)", text("services/battery.h")) is not None)
for rel, need in (("ui/ota_github.cpp", "STR_GH_OTA_SUPPLY"), ("web/pages/page_firmware.cpp", "STR_W_FW_SUPPLY")):
    c = code(rel)
    expect(f"src/{rel}: asks batteryUpdateBlock()", "batteryUpdateBlock(" in c)
    expect(f"src/{rel}: says {need} when the supply is not known", need in c and "BLOCK_SUPPLY" in c)
expect("web/pages/page_firmware.cpp: /api/ota/install refuses with the error \"supply\"",
       '\\"supply\\"' in text("web/pages/page_firmware.cpp"))
lang_h = text("lang.h")
lang_cpp = text("lang.cpp")
for sid in ("STR_GH_OTA_SUPPLY", "STR_W_FW_SUPPLY"):
    expect(f"lang.h: {sid}", re.search(rf"\b{sid}\b", lang_h) is not None)
    expect(f"lang.cpp: a row for {sid}", re.search(rf"//\s*{sid}\b", lang_cpp) is not None)

# 6. Nothing of the diagnosis firmware.
for p in src.rglob("*"):
    if p.is_file() and p.suffix in (".cpp", ".h"):
        t = p.read_text(encoding="utf-8", errors="replace")
        rel = p.relative_to(src).as_posix()
        for word in ("esp_sleep_enable_timer_wakeup", "/api/diag/sleep", "SSC_SLEEP_DIAG", "sleep_diag"):
            expect(f"src/{rel}: nothing of the diagnosis firmware ({word})", word not in t)

if failures:
    print("sleep power wiring: FAILED")
    for f in failures:
        print("  -", f)
    sys.exit(1)
print("sleep power wiring: ok")
