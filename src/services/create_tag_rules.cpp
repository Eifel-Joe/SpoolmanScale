#include "services/create_tag_rules.h"

#include <string.h>

// The length of a Bambu tray uuid, as tagIsBambu() and the copy flow count it.
#define CREATE_TAG_BAMBU_UUID_LEN 32

const char* createPadTagKey(const char* tray_uuid, const char* uid_str) {
  if (tray_uuid && strlen(tray_uuid) == CREATE_TAG_BAMBU_UUID_LEN) return tray_uuid;
  return uid_str ? uid_str : "";
}

bool createCardTagFits(const char* held, const char* now) {
  if (!held || !held[0]) return true;
  if (!now || !now[0]) return true;
  return strcmp(held, now) == 0;
}
