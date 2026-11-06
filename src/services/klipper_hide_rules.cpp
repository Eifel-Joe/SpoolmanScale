#include "services/klipper_hide_rules.h"

#include <string.h>

#include "services/klipper_store_rules.h"   // klipperNameTaken

namespace {

// As the Klipper page trims a typed name (klipper_store_rules.cpp).
bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Happy Hare's location of a gate, the blanks at both ends already gone:
// "<host> @ MMU Gate:<digits>", exactly as mmu_server.py writes it.
bool isMmuGate(const char* begin, const char* end) {
  static const char MARK[] = " @ MMU Gate:";
  const size_t mark_len = sizeof(MARK) - 1;
  const char* d = end;
  while (d > begin && d[-1] >= '0' && d[-1] <= '9') d--;
  if (d == end) return false;                               // no number
  if ((size_t)(d - begin) < mark_len + 1) return false;     // no host in front
  return memcmp(d - mark_len, MARK, mark_len) == 0;
}

}  // namespace

bool klipperLocationHidden(const char* location, const KlipperHide& hide) {
  if (!location) return false;
  const char* begin = location;
  while (*begin != '\0' && isBlank(*begin)) begin++;
  const char* end = begin + strlen(begin);
  while (end > begin && isBlank(end[-1])) end--;
  if (begin == end) return false;

  if (hide.mmu && isMmuGate(begin, end)) return true;
  if (!hide.printers.name || hide.printers.count <= 0) return false;

  // klipperNameTaken() compares whole texts: the trimmed one goes in a copy.
  // A location longer than this cannot be a printer's name (KL_NAME_MAX).
  char trimmed[KL_LOC_MAX];
  const size_t len = (size_t)(end - begin);
  if (len >= sizeof(trimmed)) return false;
  memcpy(trimmed, begin, len);
  trimmed[len] = '\0';
  return klipperNameTaken(trimmed, hide.printers.name, hide.printers.count, -1);
}

KlipperLocRow klipperLocationRow(const char* location, const KlipperHide& hide,
                                 int shown, int limit) {
  if (klipperLocationHidden(location, hide)) return KLR_HIDE;
  return shown >= limit ? KLR_CUT : KLR_SHOW;
}
