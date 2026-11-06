#include "services/label_printer_roll.h"

#include <stdio.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/brother_ql.h"
#include "services/brother_ql_status.h"

// What the printer said at the last refusal for a roll, and the roll set
// then. In memory only; a few bytes.
struct KeptRoll {
  bool            valid;     // the last refusal was another roll or an unknown one
  BrotherQlStatus seen;
  uint8_t         set_w, set_l, set_type;
};
static KeptRoll s_roll = {};

static LabelPrintResult resultOf(BrotherQlRollVerdict v) {
  switch (v) {
    case BQV_OK:
    case BQV_UNCHECKED: return LP_OK;
    case BQV_OTHER: return LP_NET_ROLL_OTHER;
    case BQV_UNKNOWN: return LP_NET_ROLL_UNKNOWN;
    case BQV_NO_ROLL: return LP_NET_NO_ROLL;
    case BQV_COVER: return LP_NET_COVER;
    case BQV_ROLL_END: return LP_NET_ROLL_END;
    case BQV_ERROR: return LP_NET_PRN_ERROR;
  }
  return LP_OK;
}

static const char* verdictName(BrotherQlRollVerdict v) {
  switch (v) {
    case BQV_OK:        return "the roll set";
    case BQV_UNCHECKED: return "a media type not known, not checked";
    case BQV_OTHER:     return "another roll";
    case BQV_UNKNOWN:   return "a roll the scale has no layout for";
    case BQV_NO_ROLL:   return "no roll";
    case BQV_COVER:     return "cover open";
    case BQV_ROLL_END:  return "roll at its end";
    case BQV_ERROR:     return "printer error";
  }
  return "?";
}

LabelPrintResult labelPrinterRollCheck(const char* host, const BrotherQlLabel& set) {
  BrotherQlStatus s{};
  if (!brotherQlAskStatus(host, &s)) return LP_OK;   // no answer: as before (V78)
  const BrotherQlRollVerdict v = brotherQlRollVerdict(s, set);
  const LabelPrintResult r = resultOf(v);
  s_roll.valid    = (v == BQV_OTHER || v == BQV_UNKNOWN);
  s_roll.seen     = s;
  s_roll.set_w    = set.width_mm;
  s_roll.set_l    = set.length_mm;
  s_roll.set_type = set.media_type;
  logSDf("Printer: Brother roll check: %s (set %ux%u %s), %s", verdictName(v),
         (unsigned)set.width_mm, (unsigned)set.length_mm,
         set.media_type == BROTHER_QL_CONTINUOUS ? "continuous" : "die-cut",
         r == LP_OK ? "printing" : "nothing sent");
  return r;
}

// A roll as the lists name it: continuous tape by its width, "62 mm
// Endlos", a die-cut label by both, "62 x 29 mm" (labelMediaName()). Here
// from the printer's own bytes, which may name a roll the table lacks.
static void rollName(bool continuous, uint8_t w, uint8_t l, char* out, size_t n) {
  if (continuous) snprintf(out, n, T(STR_PRN_MEDIA_CONT), (unsigned)w);
  else snprintf(out, n, T(STR_PRN_MEDIA_FMT), (unsigned)w, (unsigned)l);
}

bool labelPrinterRollDetail(char* out, size_t n) {
  if (!out || !n) return false;
  out[0] = '\0';
  if (!s_roll.valid) return false;
  char seen[24], set[24];
  rollName(s_roll.seen.media == BQS_CONTINUOUS, s_roll.seen.width_mm, s_roll.seen.length_mm,
           seen, sizeof(seen));
  rollName(s_roll.set_type == BROTHER_QL_CONTINUOUS, s_roll.set_w, s_roll.set_l, set, sizeof(set));
  snprintf(out, n, T(STR_PRN_NET_ROLL_DETAIL), seen, set);
  return true;
}

bool labelPrinterSeenRollName(char* out, size_t n) {
  if (!out || !n) return false;
  out[0] = '\0';
  const BrotherQlLabel* l = s_roll.valid ? brotherQlStatusLabel(s_roll.seen) : nullptr;
  if (!l) return false;
  rollName(l->media_type == BROTHER_QL_CONTINUOUS, l->width_mm, l->length_mm, out, n);
  return true;
}

bool labelPrinterSwitchToSeenRoll() {
  const BrotherQlLabel* l = s_roll.valid ? brotherQlStatusLabel(s_roll.seen) : nullptr;
  if (!l || labelPrinterLink() != LP_LINK_WLAN) {
    logSD("Printer: switch to the roll in the Brother not made, no roll of the table or not the Brother");
    return false;
  }
  LabelPrinterConfig c = labelPrinterLoadConfig();
  c.media_width_mm  = l->width_mm;
  c.media_length_mm = l->length_mm;
  const bool ok = labelPrinterSaveConfig(c);
  logSDf("Printer: label stock -> %ux%u mm, the roll in the Brother%s",
         (unsigned)c.media_width_mm, (unsigned)c.media_length_mm, ok ? "" : ", not saved");
  return ok;
}

int labelPrintTitleOfText(int text) {
  switch (text) {
    case STR_PRN_NET_SENT_MSG: return STR_PRN_NET_SENT_TITLE;
    case STR_PRN_NET_ROLL_MSG: return STR_PRN_NET_ROLL_TITLE;
    case STR_PRN_NET_ROLL_UNKNOWN_MSG: return STR_PRN_NET_ROLL_TITLE;
    case STR_PRN_NET_NO_ROLL_MSG: return STR_PRN_NET_NO_ROLL_TITLE;
    case STR_PRN_NET_COVER_MSG: return STR_PRN_NET_COVER_TITLE;
    case STR_PRN_NET_ROLL_END_MSG: return STR_PRN_NET_ROLL_END_TITLE;
    case STR_PRN_NET_PRN_ERROR_MSG: return STR_PRN_NET_PRN_ERROR_TITLE;
    default: return -1;
  }
}
