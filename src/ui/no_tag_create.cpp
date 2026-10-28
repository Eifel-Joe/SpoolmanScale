#include "ui/no_tag_create.h"

#include "hardware/sd_logger.h"
#include "lang.h"

static bool s_no_tag = false;

void noTagModeSet(bool on) {
  if (on == s_no_tag) return;
  s_no_tag = on;
  // The reason is fixed by the value: on has one way in, off has several (a
  // tag on the pad, another entry, the end of a create) and the SHOW and
  // BTN lines before this one name which.
  logSD(on ? "NoTag: mode on (no tag and no id on the pad)"
           : "NoTag: mode off (another way in, or the create is over)");
}

bool noTagMode() {
  return s_no_tag;
}

const char* noTagEntryTitle() {
  return T(s_no_tag ? STR_BTN_CREATE_SPOOL : STR_BTN_COPY_SPOOL);
}
