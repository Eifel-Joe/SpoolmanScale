#include "services/label_fact_size.h"

int labelFactPick(const LabelFactFit* fits, int n, const LabelFactRoom& room) {
  if (!fits) return -1;
  for (int i = 0; i < n; i++) {
    const LabelFactFit& f = fits[i];
    if (!f.spells) continue;
    if (f.width > room.width || f.height > room.height) continue;
    if (f.code < room.code_min) continue;
    return i;
  }
  return -1;
}
