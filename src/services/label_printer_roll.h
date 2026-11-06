#pragma once

#include <stddef.h>

#include "services/brother_ql_rules.h"
#include "services/label_printer.h"

// ============================================================
//  THE ROLL IN THE BROTHER, ON THE WAY OF A PRINT (Spec 5.3 L1, V78)
//
//  Before a label goes to a QL-810W/820NWB, labelPrinterPrint()'s
//  Brother way asks the printer which roll it holds (brother_ql.h,
//  brotherQlAskStatus) and judges it against the roll set
//  (brother_ql_status.h). When the printer would not print the
//  label anyway - another roll, one the scale has no layout for,
//  none, an open cover, a roll at its end, an error - nothing is
//  sent, and the result says why. No answer within 1 s, or an
//  answer the scale cannot judge, prints as before: the check must
//  never keep a label from a printer that can print it.
//
//  Kept here: what the printer said at the last refusal for a roll,
//  for the two lines under the card and the browser's last-print
//  line, and for the card's button that sets the scale to the roll
//  in the printer (Joerg 06.11.2026: switch the scale over, or
//  change the roll, then print again). The button only switches;
//  the next print is the user's.
//
//  Its own file: label_printer.cpp stands near the 1000 lines of
//  the house rules.
// ============================================================

// Asks the printer at `host` and judges its roll against `set`. LP_OK to
// print; otherwise the result in the print's place (LP_NET_ROLL_OTHER,
// LP_NET_ROLL_UNKNOWN, LP_NET_NO_ROLL, LP_NET_COVER, LP_NET_ROLL_END,
// LP_NET_PRN_ERROR), and nothing has been sent. Blocking, at most 1 s.
LabelPrintResult labelPrinterRollCheck(const char* host, const BrotherQlLabel& set);

// The two lines under a refusal for another roll or one the scale cannot
// print on, in the language set: "In the printer: 62 x 29 mm\nSet: 62 mm
// continuous". False and "" when the last refusal was for none of these.
bool labelPrinterRollDetail(char* out, size_t n);

// The name of the roll in the printer at that refusal, when it is one the
// scale has: for the button that switches to it. False otherwise.
bool labelPrinterSeenRollName(char* out, size_t n);

// Sets the Brother's roll at the scale to the one in the printer at the last
// refusal, as the size list would, with its own print position. False when
// there is none the scale has, the label printer is no longer the Brother,
// or the save failed.
bool labelPrinterSwitchToSeenRoll();

// The title over one of the Brother's own sentences ("sent", and those of
// the roll check), for the card and the test print row; -1 for any other.
int labelPrintTitleOfText(int text);
