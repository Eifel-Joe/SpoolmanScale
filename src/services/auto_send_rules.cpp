#include "services/auto_send_rules.h"

#include <math.h>

#include "services/auto_weight_state.h"
#include "services/spool_extras_rules.h"

AutoSendVerdict autoSendVerdict(float gross_g, float parts_g, float spool_g, float stored_g) {
  float netto = extrasBare(gross_g, parts_g) - spool_g;
  if (netto < 0.0f) netto = 0.0f;
  AutoSendVerdict v = { AUTO_SEND_WRITE, netto, netto - stored_g };
  // The rise first: a spool more than 5 g heavier is never written on its own.
  if (v.diff_g > AUTO_WEIGHT_RISE_G) {
    v.kind = AUTO_SEND_RISE;
  } else if (fabsf(v.diff_g) < AUTO_WEIGHT_MIN_CHANGE_G) {
    v.kind = AUTO_SEND_UNCHANGED;
  }
  return v;
}
