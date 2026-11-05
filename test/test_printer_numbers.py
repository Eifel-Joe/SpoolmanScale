"""The printer numbers of v0.8.7-beta.6 (Spec 5.3 U3/AC, V74): the number a scale stores for its Bluetooth printer (printer_model) and the lists the pages build from the models.

The original gives its NIIMBOT D110 the number 10, which was our Brother's. Our Brother never sat in printer_model - it has keys of its own (printer_link, printer_host, px_brother) - so its number lives only in the running firmware and moves to 100; the fork's own printers count from 100, well away from the original's next numbers, and the TYPONOS PM240 is 101. A stored number that is no Bluetooth printer (the Brother's, or one the scale does not know) is taken as the factory model, never as the Brother over Bluetooth.

Compiles the header for the numbers; reads the source for where they are used."""
import subprocess
import tempfile
from pathlib import Path

import klipper_src as ks

root = Path(__file__).parents[1]
ks.assert_ci_step("test_printer_numbers.py")

source = r'''
#include "services/label_printer.h"

// The numbers are NVS content: spelled out here, not taken from the header.
static_assert(LP_MODEL_M220 == 1 && LP_MODEL_M110 == 2 && LP_MODEL_M100 == 3, "Phomemo, as the original");
static_assert(LP_MODEL_NIIMBOT_B == 4 && LP_MODEL_NIIMBOT_M2 == 5, "NIIMBOT, as the original");
static_assert(LP_MODEL_M120 == 6 && LP_MODEL_M200 == 7 && LP_MODEL_M221 == 8 && LP_MODEL_M110S == 9,
              "Phomemo, as the original");
static_assert(LP_MODEL_NIIMBOT_D110 == 10, "the D110 keeps the original's number");
static_assert(LP_MODEL_BROTHER_QL820 == 100, "the Brother, only in memory, the fork's own from 100");
static_assert(LP_MODEL_PM240 == 101, "the TYPONOS PM240, the fork's own");
static_assert(LP_PROTO_TSPL != LP_PROTO_PHOMEMO_M220 && LP_PROTO_TSPL != LP_PROTO_PHOMEMO_M110 &&
              LP_PROTO_TSPL != LP_PROTO_NIIMBOT && LP_PROTO_TSPL != LP_PROTO_BROTHER_QL,
              "TSPL is a protocol of its own");

int main() { return 0; }
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                    "-I", str(root / "src"), str(cpp)], check=True)

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


LP = "services/label_printer.cpp"
code = ks.code_of(LP)

# 1. The profiles and the lists: the D110 and the PM240 are Bluetooth models,
#    the PM240 experimental and on its own protocol; the Brother only under WLAN.
need("PROFILE_NIIMBOT_D110" in code and "PROFILE_PM240" in code,
     f"src/{LP}: no profile for the D110 or the PM240")
start = code.find("const LabelPrinterModel LABEL_PRINTER_MODELS[]")
bt_list = code[start:code.find("};", start)] if start >= 0 else ""
need("LP_MODEL_NIIMBOT_D110" in bt_list and "LP_MODEL_PM240" in bt_list,
     f"src/{LP}: the D110 or the PM240 is missing from LABEL_PRINTER_MODELS")
need("LP_MODEL_BROTHER_QL820" not in bt_list,
     f"src/{LP}: the Brother is in the Bluetooth list")
start = code.find("LABEL_PRINTER_MODELS_WLAN[]")
wlan_list = code[start:code.find("};", start)] if start >= 0 else ""
need(ks.squeeze(wlan_list).endswith("{LP_MODEL_BROTHER_QL820"),
     f"src/{LP}: the WLAN list is not the Brother alone")
profile = ks.body_of(LP, "labelPrinterProfile")
for model in ("LP_MODEL_NIIMBOT_D110", "LP_MODEL_PM240", "LP_MODEL_BROTHER_QL820"):
    need(f"case {model}:" in profile, f"src/{LP}: labelPrinterProfile() lacks {model}")
pm240 = ks.squeeze(ks.source_of(LP).code_str)
start = pm240.find("PROFILE_PM240={")
row = pm240[start:pm240.find("};", start)] if start >= 0 else ""
need('"TYPONOS","PM240"' in row, f"src/{LP}: the PM240 profile does not say TYPONOS PM240")
need(",400,true,true,203," in row,
     f"src/{LP}: the PM240 profile is not experimental and direct thermal")
need(row.endswith("LP_PROTO_TSPL"), f"src/{LP}: the PM240 profile is not on LP_PROTO_TSPL")

# 2. What printer_model holds is a Bluetooth printer or the factory model: the
#    normalising of the Bluetooth config refuses the Brother's protocol.
norm = ks.squeeze(ks.body_of(LP, "normalized"))
need("isBrother(p)" in norm or "LP_PROTO_BROTHER_QL" in norm,
     f"src/{LP}: normalized() takes the Brother's number from printer_model as it is")

# 3. The print goes to the TSPL driver for the PM240.
pr = ks.squeeze(ks.body_of(LP, "labelPrinterPrint"))
need("LP_PROTO_TSPL" in pr and "tsplPrint(" in ks.squeeze(code),
     f"src/{LP}: labelPrinterPrint() has no way to the TSPL driver")

# 4. The size lists of the pages carry the models of the chosen link, the
#    Brother's under WLAN too (our fitList() did; the original's helper lists
#    the Bluetooth models only).
fit = ks.squeeze(ks.body_of("web/web_printer_select.cpp", "webPrinterFitList"))
need("labelPrinterModels(labelPrinterLink()" in fit,
     "src/web/web_printer_select.cpp: webPrinterFitList() does not list the chosen link's models")
need("LABEL_PRINTER_MODELS[" not in fit,
     "src/web/web_printer_select.cpp: webPrinterFitList() walks the Bluetooth list")

# 5. The error for a device without the printers' write characteristic names
#    no maker: the PM240 is reached the same way as a Phomemo.
rows = ks.lang_rows()
ks.assert_row_complete(rows, "STR_PRN_ERR_NOT_PRINTER", "src/lang.cpp")
for cell in rows["STR_PRN_ERR_NOT_PRINTER"]:
    need("Phomemo" not in cell, f"src/lang.cpp: STR_PRN_ERR_NOT_PRINTER still says Phomemo: {cell}")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_printer_numbers: ok")
