#pragma once

#include <Arduino.h>
#include <WebServer.h>

// Every setting of this scale as one JSON file, and back again.
//
// GET  /api/backup             the file without secrets (GATE_CONFIG)
// GET  /api/backup?secrets=1   with the Wi-Fi login, the API keys and the
//                              address the Spoolman secret is bound to
//                              (GATE_MAINT: a file like that opens the
//                              network and the backends to whoever holds it)
// POST /api/restore            the file as the body (GATE_MAINT)
//
// A restore is exact. The file is checked whole first, and a single bad
// entry refuses it before anything has changed. Then every key on the device
// that travels with this file is erased and the file's entries are written,
// so a setting that was still at its default when the file was made is back
// at its default afterwards. Then /api/restore restarts the scale itself:
// the values are read at boot, and a scale running on half old and half new
// settings would be in a state nobody chose.
//
// Which key travels is decided in backup_rules.h, one rule for the file, the
// erase and the import. Secrets only travel in a file made with them, and a
// file made without them leaves the device's own untouched. What belongs to
// this device rather than to its owner never travels: the firmware version
// and the update bookkeeping, what was found out about the NFC reset line,
// the log clear sequence (log_clr_seq), the web access switches and the
// password.
void webBackupRoutes(WebServer &srv);

// The card for a settings page. Expects webShellRestartUi() on the same page:
// it brings rtSd() and restartWait(), which the card uses to wait for the
// restart a restore ends with.
String webBackupCard();
