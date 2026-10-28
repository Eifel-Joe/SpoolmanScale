#include "ui/no_tag_create.h"

#include "app/app_loop.h"
#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "ui/spool_flow.h"
// Last, see the note on ArduinoJson and T() in tag_create_popup.cpp.
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

// Wurzel: a lifted tag leaves its ids (g_tag, link_tag_uid) until the 60 s timer
// or the clear button empties the display, but the button already reads
// "Create spool" the moment the tag is gone. The choice would then decide the
// flag from those ids (off, "have an id"), show the lifted tag's UID and link
// the new spool to a tag that is no longer there.
// Fix: on an empty screen with leftovers, drop them first, the way the timer
// does (dropLiftedTagDisplay() also ends the timer's wait), then open the
// choice. In any other state this only opens it, as before.
// NOT-TO-DO: clearing inside showCopyEntryPopup(): it is also the way back from
// the database and the sort entry, with a tag possibly on the pad again.
// See test_no_tag_entry.py.
void noTagEntryTap() {
  if (!tag_present && !sm_found &&
      (g_tag.uid_str[0] || g_tag.tray_uuid[0] || link_tag_uid[0])) {
    logSD("NoTag: create spool drops the leftovers of a lifted tag");
    dropLiftedTagDisplay();
  }
  showCopyEntryPopup();
}

const char* noTagEntryTitle() {
  return T(s_no_tag ? STR_BTN_CREATE_SPOOL : STR_BTN_COPY_SPOOL);
}
