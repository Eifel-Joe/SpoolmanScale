"""Read the source tree without a compiler and check how the battery of the 7B (Spec 5.3 AB, V60) is wired in: only the 7B measures (BOARD_BATTERY_SENSE, the 7 ends its divider at a test pad, the WT32 has no battery), the main loop drives it, every way that writes a firmware asks batteryUpdateBlocked() before the first byte (the update at the scale, the update from the web page, the upload; Spec 5.3 T), the header line builds its label only where the board measures, the header font holds the battery and charge symbols, status.json reports it, the simulated battery is a route of the acceptance firmware only, and every new text has its three languages."""
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


# 1. Which board measures. The 7B reads VBAT/3 on its expander's ADC; the 7's
#    divider ends at test pad TP1 and its CH422G has no ADC; the WT32 has no
#    battery connector (docs/recherche/2026-11-03-beta20 of the meta repo).
boards = {"waveshare_s3_lcd7b": "1", "waveshare_s3_lcd7": "0", "wt32_sc01_plus": "0"}
for board, want in boards.items():
    m = re.search(r"^#define\s+BOARD_BATTERY_SENSE\s+(\d+)\s*$", text(f"boards/{board}/board_config.h"), re.M)
    expect(f"boards/{board}/board_config.h: BOARD_BATTERY_SENSE {want}", m is not None and m.group(1) == want,
           m.group(1) if m else "not defined")
expect("boards/board.h: BOARD_BATTERY_SENSE is in the list of what a board_config.h provides",
       "BOARD_BATTERY_SENSE" in text("boards/board.h"))

# 2. Only the 7B's folder reads the battery, through the expander.
readers = [p.relative_to(src).as_posix() for p in src.rglob("*.cpp")
           if re.search(r"\bbool\s+batteryReadRaw\s*\(", cc.Source(p.read_text(encoding="utf-8")).code)]
expect("batteryReadRaw() is defined once, in the 7B's folder",
       len(readers) == 1 and readers[0].startswith("boards/waveshare_s3_lcd7b/"), str(readers))
if len(readers) == 1:
    expect(f"src/{readers[0]}: batteryReadRaw() reads io_ch32v003::readAdc()", "io_ch32v003::readAdc(" in code(readers[0]))

# 3. The service: the rules decide, the board reads, the main loop drives.
service = code("services/battery.cpp")
expect("services/battery.cpp: feeds the rules (bat::feed)", "bat::feed(" in service)
expect("services/battery.cpp: measures only where BOARD_BATTERY_SENSE", "#if BOARD_BATTERY_SENSE" in text("services/battery.cpp"))
expect("services/battery.cpp: asks bat::updateBlocked()", "bat::updateBlocked(" in service)
expect("app/app_loop.cpp: calls batteryTick()", "batteryTick(" in code("app/app_loop.cpp"))

# 4. Every way that writes a firmware asks first (Spec 5.3 T, V60): the
#    download from GitHub at the scale and from the web page, and the upload.
#    The question stands in the 60 lines before the write starts, in the same
#    file; github_release.cpp, where githubFlashTag() itself begins, is the
#    callee and asks nothing.
starts = []
for path in sorted(src.rglob("*.cpp")):
    rel = path.relative_to(src).as_posix()
    if rel == "services/github_release.cpp":
        continue
    lines = cc.Source(path.read_text(encoding="utf-8")).code.splitlines()
    for i, line in enumerate(lines):
        if re.search(r"\bgithubFlashTag\s*\(|\bUpdate\.begin\s*\(", line):
            starts.append(rel)
            before = "\n".join(lines[max(0, i - 60):i])
            expect(f"src/{rel}:{i + 1}: a firmware write starts without batteryUpdateBlocked() before it",
                   "batteryUpdateBlocked(" in before)
expect("the three ways to write a firmware are found (ota_github, page_firmware twice)",
       starts.count("ui/ota_github.cpp") >= 1 and starts.count("web/pages/page_firmware.cpp") >= 2, str(starts))
firmware_page = code("web/pages/page_firmware.cpp")
expect("web/pages/page_firmware.cpp: /api/ota/install refuses with the error \"battery\"",
       "batteryUpdateBlocked(" in firmware_page and '\\"battery\\"' in text("web/pages/page_firmware.cpp"))

# 5. The header line: a label of its own, built only where the board
#    measures, placed by the rules' headerSlot(), in the header's font 12,
#    which has the five battery symbols and the bolt (F240..F244, F0E7).
main_screen = text("ui/main_screen.cpp")
m = re.search(r"#if BOARD_BATTERY_SENSE(.*?)#endif", main_screen, re.S)
expect("ui/main_screen.cpp: lbl_hdr_bat is created under #if BOARD_BATTERY_SENSE",
       m is not None and re.search(r"lbl_hdr_bat\s*=\s*lv_label_create\(hdr\)", m.group(1)) is not None)
expect("ui/header_status.cpp: places the battery with bat::headerSlot()", "bat::headerSlot(" in code("ui/header_status.cpp"))
font = text("fonts/lv_font_montserrat_ext_12.c")
opts = re.search(r"Opts:.*", font).group(0)
for cp in (61671, 62016, 62017, 62018, 62019, 62020):
    expect(f"fonts/lv_font_montserrat_ext_12.c: glyph {cp:#x} is in the font", re.search(rf"\b{cp}\b", opts) is not None)

# 6. status.json says it on the 7B, the simulated battery only in the
#    acceptance firmware's routes.
status = text("web/pages/page_status.cpp")
m = re.search(r"#if BOARD_BATTERY_SENSE(.*?)#endif", status, re.S)
expect("web/pages/page_status.cpp: status.json has \"battery\" under #if BOARD_BATTERY_SENSE",
       m is not None and '\\"battery\\"' in m.group(1))
holders = [p.relative_to(src).as_posix() for p in src.rglob("*.cpp") if "/api/sim/battery" in p.read_text(encoding="utf-8")]
expect("/api/sim/battery is spelled only in web/web_doc.cpp", holders == ["web/web_doc.cpp"], str(holders))

# 7a. The start on an empty battery (Spec V66): read right after the expander
#     is up, before the settings, the logger, the panel and the WiFi; nothing
#     is written while it waits, the line comes once the logger is up; the
#     reading of the next start is simulated only in the acceptance firmware.
boot = code("app/app_boot.cpp")
order = [boot.find(s) for s in ("boardEarlyInit();", "batteryStartCheck();", "loadPrefs();", "initSD();",
                                 "batteryStartReport();", "displayHardwareBegin(")]
expect("app/app_boot.cpp: boardEarlyInit(), batteryStartCheck(), loadPrefs(), initSD(), batteryStartReport(), "
       "displayHardwareBegin() in this order", all(i >= 0 for i in order) and order == sorted(order), str(order))
bat_src = text("services/battery.cpp")
m = re.search(r"void batteryStartCheck\(\) \{(.*?)\n\}", cc.Source(bat_src).code, re.S)
expect("services/battery.cpp: batteryStartCheck() is there", m is not None)
if m:
    body = m.group(1)
    for needle in ("emptyAtStart(", "goAfterWait(", "startLook()"):
        expect(f"services/battery.cpp: batteryStartCheck() uses {needle}", needle in body)
    for banned in ("logSD", "prefs", "Preferences", "esp_partition", "flashLog", "nvs_", "WiFi", "panel", "deep_sleep"):
        expect(f"services/battery.cpp: batteryStartCheck() calls nothing with {banned} (nothing is written, "
               "nothing powered, no sleep that keeps it dark after a plug)", banned not in body)
expect("services/battery.cpp: batteryStartCheck() is defined under BOARD_BATTERY_SENSE and as a no-op without",
       bat_src.count("void batteryStartCheck()") == 2)
doc = text("web/web_doc.cpp")
expect("web/web_doc.cpp: boot=1 sets the next start's reading (batterySimForStart)",
       'srv.arg("boot")' in doc and "batterySimForStart(" in doc)
holders = [p.relative_to(src).as_posix() for p in src.rglob("*.cpp")
           if "batterySimForStart(" in p.read_text(encoding="utf-8")]
expect("batterySimForStart() is used only by the acceptance routes and defined in services/battery.cpp",
       sorted(holders) == ["services/battery.cpp", "web/web_doc.cpp"], str(holders))
expect("services/battery.cpp: the simulated start reading lives under SSC_ACCEPTANCE",
       re.search(r"#if defined\(SSC_ACCEPTANCE\)\s*//[^\n]*\n(?://[^\n]*\n)*struct StartSim", bat_src) is not None)

# 7. The texts, each in German, English and French.
lang_h = text("lang.h")
lang_cpp = text("lang.cpp")
for sid in ("STR_BAT_LOW_TITLE", "STR_BAT_LOW_TEXT", "STR_BAT_CRIT_TITLE", "STR_BAT_CRIT_TEXT",
            "STR_GH_OTA_BATTERY", "STR_W_FW_BATTERY"):
    expect(f"lang.h: {sid}", re.search(rf"\b{sid}\b", lang_h) is not None)
    expect(f"lang.cpp: a row for {sid}", re.search(rf"//\s*{sid}\b", lang_cpp) is not None)

if failures:
    print("battery wiring: FAILED")
    for f in failures:
        print("  -", f)
    sys.exit(1)
print("battery wiring: ok")
