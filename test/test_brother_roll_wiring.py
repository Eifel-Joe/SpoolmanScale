"""Which roll is in the Brother, asked before every label (v0.8.7-beta.8, Spec V78): where the check sits on the way of a print, which printers it asks, what the scale says when it sends nothing - on the print card, in the browser's last-print line and in the test print row - and the card's button that switches the scale to the roll in the printer. The rules of the request and the verdict are compiled in test_brother_ql_status.py; here the wiring is read from the source, and the result values are compiled against the reach rules."""
import subprocess
import tempfile
from pathlib import Path

import klipper_src as ks

root = Path(__file__).parents[1]
ks.assert_ci_step("test_brother_roll_wiring.py")

NEW = ["LP_NET_ROLL_OTHER", "LP_NET_ROLL_UNKNOWN", "LP_NET_NO_ROLL", "LP_NET_COVER",
       "LP_NET_ROLL_END", "LP_NET_PRN_ERROR"]

# The new results are the Brother's own: not over Bluetooth, so the header's
# Bluetooth symbol keeps its colour (Spec 5.3 AC f21).
source = "#include \"services/label_printer_reach.h\"\n#include <cassert>\nint main() {\n"
source += "".join(f"  assert(labelPrintReach({r}) == LP_REACH_KEEP);\n" for r in NEW)
source += "  return 0;\n}\n"
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


LP = "services/label_printer.cpp"
ROLL = "services/label_printer_roll.cpp"

# 1. On the way of every Brother label, before a byte goes out: asked, for the
#    QL-810W/820NWB only. The QL-700 behind a forwarder has no SNMP; the
#    Bluetooth printers say what they have themselves.
pb = ks.body_of(LP, "printBrother")
i_check, i_send = pb.find("labelPrinterRollCheck("), pb.find("brotherQlPrint(")
need(i_check >= 0 and i_send > i_check,
     f"src/{LP}: printBrother() does not ask for the roll before it sends")
need(i_check >= 0 and ("BQM_QL800" in pb[:i_check] or "BQM_QL700" in pb[:i_check]),
     f"src/{LP}: printBrother() asks the QL-700 behind its forwarder as well")
need("labelPrinterRollCheck(" not in ks.body_of(LP, "printBle"),
     f"src/{LP}: a Bluetooth printer is asked for a Brother's roll")
lpp = ks.body_of(LP, "labelPrinterPrint")
need(lpp.find("printBrother(") >= 0, f"src/{LP}: labelPrinterPrint() no longer takes the Brother's way")

# 2. The check: the printer's answer, the verdict against the roll set, a
#    result for each verdict; no answer, or one the scale cannot judge,
#    prints as before (V78: nothing hangs on the check).
rc = ks.body_of(ROLL, "labelPrinterRollCheck")
need("brotherQlAskStatus(" in rc and "brotherQlRollVerdict(" in rc,
     f"src/{ROLL}: labelPrinterRollCheck() does not ask the printer and judge its roll")
i_ask = rc.find("brotherQlAskStatus(")
need(i_ask >= 0 and "return LP_OK" in rc[i_ask:i_ask + 160],
     f"src/{ROLL}: labelPrinterRollCheck() does not print as before when the printer does not answer")
roll_src = ks.code_of(ROLL)
for v, r in [("BQV_OTHER", "LP_NET_ROLL_OTHER"), ("BQV_UNKNOWN", "LP_NET_ROLL_UNKNOWN"),
             ("BQV_NO_ROLL", "LP_NET_NO_ROLL"), ("BQV_COVER", "LP_NET_COVER"),
             ("BQV_ROLL_END", "LP_NET_ROLL_END"), ("BQV_ERROR", "LP_NET_PRN_ERROR")]:
    need(f"case {v}: return {r};" in roll_src, f"src/{ROLL}: {v} does not become {r}")
need("case BQV_OK:" in roll_src and "case BQV_UNCHECKED:" in roll_src,
     f"src/{ROLL}: the roll that is set, or one that cannot be judged, does not print")

# 3. The driver: UDP to the printer's SNMP port, two tries, only the
#    printer's own answer to this request, the socket closed again.
snmp = ks.body_of("services/brother_ql_snmp.cpp", "brotherQlAskStatus")
for part in ("brotherQlStatusRequest(", "brotherQlStatusParse(", "BROTHER_QL_STATUS_TRIES",
             "BROTHER_QL_STATUS_WAIT_MS", "BROTHER_QL_SNMP_PORT", "recvfrom(", "close("):
    need(part in snmp, f"src/services/brother_ql_snmp.cpp: brotherQlAskStatus() lacks {part}")
need("sin_addr.s_addr !=" in snmp or "sin_addr.s_addr ==" in snmp,
     "src/services/brother_ql_snmp.cpp: an answer from another address is taken as the printer's")
need("bool brotherQlAskStatus(const char* host, BrotherQlStatus* out);" in
     ks.collapse((ks.src / "services/brother_ql.h").read_text(encoding="utf-8")),
     "src/services/brother_ql.h: brotherQlAskStatus() is not declared")

# 4. The texts: each new result has its sentence, and on the card its title.
rs = ks.body_of(LP, "labelPrintResultString")
texts = {"LP_NET_ROLL_OTHER": "STR_PRN_NET_ROLL_MSG", "LP_NET_ROLL_UNKNOWN": "STR_PRN_NET_ROLL_UNKNOWN_MSG",
         "LP_NET_NO_ROLL": "STR_PRN_NET_NO_ROLL_MSG", "LP_NET_COVER": "STR_PRN_NET_COVER_MSG",
         "LP_NET_ROLL_END": "STR_PRN_NET_ROLL_END_MSG", "LP_NET_PRN_ERROR": "STR_PRN_NET_PRN_ERROR_MSG"}
for r, t in texts.items():
    need(f"case {r}: return {t};" in rs, f"src/{LP}: labelPrintResultString() lacks {r} -> {t}")
titles = {"STR_PRN_NET_ROLL_MSG": "STR_PRN_NET_ROLL_TITLE", "STR_PRN_NET_ROLL_UNKNOWN_MSG": "STR_PRN_NET_ROLL_TITLE",
          "STR_PRN_NET_NO_ROLL_MSG": "STR_PRN_NET_NO_ROLL_TITLE", "STR_PRN_NET_COVER_MSG": "STR_PRN_NET_COVER_TITLE",
          "STR_PRN_NET_ROLL_END_MSG": "STR_PRN_NET_ROLL_END_TITLE",
          "STR_PRN_NET_PRN_ERROR_MSG": "STR_PRN_NET_PRN_ERROR_TITLE",
          "STR_PRN_NET_SENT_MSG": "STR_PRN_NET_SENT_TITLE"}
tt = ks.body_of(ROLL, "labelPrintTitleOfText")
for t, title in titles.items():
    need(f"case {t}: return {title};" in tt, f"src/{ROLL}: labelPrintTitleOfText() lacks {t} -> {title}")
card = ks.body_of("ui/print_card.cpp", "printCardResult")
need("labelPrintTitleOfText(" in card, "src/ui/print_card.cpp: the card has no title of its own for the roll")
rows = ks.lang_rows()
for sid in list(texts.values()) + sorted(set(titles.values())) + ["STR_PRN_NET_ROLL_DETAIL", "STR_PRN_NET_ROLL_SWITCH"]:
    need(ks.enum_has(sid), f"src/lang.h: {sid} is missing")
    ks.assert_row_complete(rows, sid, "test_brother_roll_wiring")
need(rows.get("STR_PRN_NET_ROLL_DETAIL", [""])[0].count("%s") == 2,
     "src/lang.cpp: STR_PRN_NET_ROLL_DETAIL does not name both rolls")
budgets = (root / "tools/ui_budgets.tsv").read_text(encoding="utf-8")
for sid in list(texts.values()) + sorted(set(titles.values())) + ["STR_PRN_NET_ROLL_SWITCH"]:
    need(f"\n{sid}\t" in budgets, f"tools/ui_budgets.tsv: no width budget for {sid}")

# 5. The card names both rolls (another roll, or one the scale cannot print
#    on), on every way a print takes at the scale, and offers to switch to
#    a roll the scale knows; the switch is made from the loop.
ps = "ui/printer_screen.cpp"
spr = ks.body_of(ps, "showPrintResult")
need("labelPrinterRollDetail(" in spr and "printCardResult(" in spr,
     f"src/{ps}: showPrintResult() does not put both rolls on the card")
for fn in ("printFixedLabel", "printSpoolLabel", "printerPrintEdit"):
    body = ks.body_of(ps, fn)
    need("showPrintResult(result)" in body, f"src/{ps}: {fn}() shows its result without the rolls")
    need("printCardResult(result)" not in body, f"src/{ps}: {fn}() still shows its result bare")
need("LP_NET_ROLL_OTHER" in card and "printer_roll_switch_pending = true" in ks.code_of("ui/print_card.cpp"),
     "src/ui/print_card.cpp: the card offers no switch to the roll in the printer")
need("labelPrinterSeenRollName(" in ks.code_of("ui/print_card.cpp"),
     "src/ui/print_card.cpp: the switch button does not name the roll")
handler = ks.body_of(ps, "handlePrinterDeferredActions")
i_flag = handler.find("printer_roll_switch_pending")
need(i_flag >= 0 and "labelPrinterSwitchToSeenRoll()" in handler[i_flag:i_flag + 200],
     f"src/{ps}: the switch is not made from the loop")
sw = ks.body_of(ROLL, "labelPrinterSwitchToSeenRoll")
need("labelPrinterSaveConfig(" in sw and "brotherQlStatusLabel(" in sw,
     f"src/{ROLL}: the switch does not save the table's roll")
need("extern bool printer_roll_switch_pending;" in ks.collapse((ks.src / "app/deferred_actions.h").read_text(encoding="utf-8")),
     "src/app/deferred_actions.h: printer_roll_switch_pending is not declared")

# 6. The browser's last-print line and the test row at the scale.
line = ks.body_of("web/web_printer_host.cpp", "webPrinterResultLine")
need("labelPrinterRollDetail(" in line, "src/web/web_printer_host.cpp: the browser's line does not name the rolls")
tv = ks.body_of("ui/bluetooth_screen.cpp", "showTestVerdict")
need("labelPrintTitleOfText(" in tv, "src/ui/bluetooth_screen.cpp: the test row shows the roll's long sentence")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_brother_roll_wiring: ok")
