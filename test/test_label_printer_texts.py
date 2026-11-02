"""Read the source tree without a compiler and check the small rests of the label printer screen and page left from the Brother over WiFi (design L1, its reviews; Punkteliste f13).

The help of the printer screen said "the printer is picked on the device card", which is the Bluetooth way only: a Brother over WiFi has no device card, its address is set in the browser. It now names both ways. The Brother's device row ended in an arrow, the sign of a row that opens a screen, while a tap only explains where the address is set; it has the help circle of an explaining row instead, and no arrow. The texts of the old Bluetooth screen and of the browser's Bluetooth switch, which the label printer screen replaced, were still in the table with nothing showing them. Two routes of the printer page refused a request with an English sentence (the page shows the answer as it is, a model chosen on a page loaded before the link changed among them); they now answer in the page's language like the address route beside them."""
import re

import klipper_src as ks

SCREEN = "ui/printer_screen.cpp"
ROUTES = ("web/pages/page_printer.cpp", "web/web_printer_host.cpp")
GONE = ("STR_BT_SWITCH", "STR_BT_SWITCH_SUB", "STR_BT_HELP", "STR_W_P_BLE_HINT")
PREFIXES = ("STR_BT_", "STR_LP_", "STR_PRN_", "STR_W_P_")

ks.assert_ci_step("test_label_printer_texts.py")

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


rows = ks.lang_rows()

# 1. The help of the printer screen names both ways to a printer.
where = "src/lang.cpp: STR_PRN_HELP"
ks.assert_row_complete(rows, "STR_PRN_HELP", where)
de, en, fr = rows["STR_PRN_HELP"]
for lang, text, words in (("German", de, ("Bluetooth", "WLAN", "Browser")),
                          ("English", en, ("Bluetooth", "WiFi", "browser")),
                          ("French", fr, ("Bluetooth", "WiFi", "navigateur"))):
    for w in words:
        need(w in text, f"{where}: the {lang} help does not name '{w}' (both ways to a printer)")

# 2. The Brother's device row explains, it opens nothing: a help circle, no arrow.
where = f"src/{SCREEN}: buildPrinterScreen(), the WLAN device row"
body = ks.body_of(SCREEN, "buildPrinterScreen", strings=True)
start = body.find("if (wlan) {")
end = body.find("} else {", start)
need(start >= 0 and end > start, f"{where}: 'if (wlan) {{ ... }} else {{' not found")
block = ks.squeeze(body[start:end])
need("makeListBtn(list,LV_SYMBOL_WIFI,buf_t,buf_s,have,&help)" in block,
     f"{where}: the row is not built with a help circle")
need("lv_obj_add_event_cb(help,infoPopupEventCb,LV_EVENT_CLICKED,"
     "INFO_POPUP_ARG(STR_PRN_DEVICE,STR_PRN_HOST_HELP))" in block,
     f"{where}: the help circle does not explain where the address is set")
need("lv_obj_add_event_cb(btn,infoPopupEventCb,LV_EVENT_CLICKED,"
     "INFO_POPUP_ARG(STR_PRN_DEVICE,STR_PRN_HOST_HELP))" in block,
     f"{where}: a tap on the row no longer explains where the address is set")
need('lv_label_set_text(lv_obj_get_child(btn,-1),"")' in block,
     f"{where}: the arrow at the end of the row is still there")
bt_block = ks.squeeze(body[end:end + 1200])
need('lv_obj_get_child(btn,-1),"")' not in bt_block,
     f"src/{SCREEN}: the Bluetooth device row, which opens the device list, lost its arrow")

# 3. No text of these screens and pages stays in the table with nothing showing it.
code = ""
for path in ks.src.rglob("*"):
    if path.suffix in (".cpp", ".h") and path.name not in ("lang.cpp", "lang.h"):
        code += path.read_text(encoding="utf-8", errors="replace")
used = set(re.findall(r"\bSTR_[A-Za-z0-9_]+", code))
header = (ks.src / "lang.h").read_text(encoding="utf-8")
enum = header[header.index("enum StringID"):]
enum = re.sub(r"//[^\n]*", "", enum[enum.index("{") + 1:enum.index("STR_COUNT")])
for sid in re.findall(r"\bSTR_[A-Za-z0-9_]+", enum):
    if sid.startswith(PREFIXES) and sid not in used:
        problems.append(f"src/lang.h: {sid} is in the table, but no screen or page uses it")
for sid in GONE:
    need(not ks.enum_has(sid), f"src/lang.h: {sid} (a text nothing shows) is still in the enum")
    need(sid not in rows, f"src/lang.cpp: {sid} (a text nothing shows) still has a row")

# 4. The printer routes refuse in the page's language.
for rel in ROUTES:
    text = ks.source_of(rel).code_str
    for _, args in ks.calls(text, "send"):
        if len(args) != 3 or not re.fullmatch(r"4\d\d", args[0]):
            continue
        body_arg = ks.collapse(args[2])
        if not body_arg.startswith("T("):
            problems.append(f"src/{rel}: send({args[0]}, ...) answers with {body_arg}, not a text of the table")
            continue
        # T(labelPrintResultString(r)) is a text of the table too, chosen at run time.
        m = re.fullmatch(r"T\((STR_\w+)\)", body_arg)
        if not m:
            continue
        try:
            ks.assert_row_complete(rows, m.group(1), f"src/{rel}: send({args[0]}, ...)")
        except AssertionError as e:
            problems.append(str(e))

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_label_printer_texts: ok")
