#include "services/create_tag_rules.h"

#include <string.h>

// The length of a Bambu tray uuid, as tagIsBambu() and the copy flow count it.
#define CREATE_TAG_BAMBU_UUID_LEN 32

static bool isBambuKey(const char* s) {
  return s && strlen(s) == CREATE_TAG_BAMBU_UUID_LEN;
}

// The bytes of a UID as the reader writes it ("7B:40:86:F6"): its hex digits,
// two to a byte.
static int uidBytes(const char* uid) {
  int digits = 0;
  for (const char* p = uid; p && *p; p++) {
    const char c = *p;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) digits++;
  }
  return digits / 2;
}

const char* createPadTagKey(const char* tray_uuid, const char* uid_str) {
  if (isBambuKey(tray_uuid)) return tray_uuid;
  return uid_str ? uid_str : "";
}

bool createCardTagFits(const char* held, const char* tray_uuid, const char* uid_str) {
  if (!held || !held[0]) return true;
  const char* now = createPadTagKey(tray_uuid, uid_str);
  if (!now[0]) return true;
  // A Bambu read that has not brought block 9 yet, see the header.
  if (isBambuKey(held) && !isBambuKey(tray_uuid) && uidBytes(uid_str) == 4) return true;
  return strcmp(held, now) == 0;
}
