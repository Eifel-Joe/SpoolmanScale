#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  SETTINGS BACKUP RULES
//
//  A settings backup is the scale's NVS namespace as a file. Which
//  keys go into it and which come back out is decided by the key
//  name alone, so the decision lives here, without Arduino, NVS or
//  JSON: a host test holds it to the lists in test_backup_rules.py,
//  and the web handler asks this unit instead of keeping lists of
//  its own.
//
//  Every key belongs to one of three groups:
//
//    setting  What somebody chose or calibrated: limits, hosts,
//             language, brightness, cal_factor, zero_offset, the
//             flags that remember a one-off migration. Travels in
//             every backup. This is the default: a key that is in
//             neither list below is a setting.
//
//    secret   Credentials: the Wi-Fi network and password and the
//             keys to FilaMan, Bambuddy and Spoolman. Travels only
//             when the file is asked to carry secrets, because a
//             backup is a file that lands on a PC or in a mailbox.
//             The consequence of that default: a new key that holds
//             a credential has to be added to the secret list, or
//             it travels in every backup.
//
//    device   Facts about this unit and its history, not choices:
//             the firmware version and the update bookkeeping, what
//             was verified about the NFC reset line and its boot
//             counters, the log clear sequence, and the web access
//             switches and password. Never travels. Restoring them
//             would make a unit report a firmware it does not run,
//             or take on what was found out about another unit.
//
//  The web access switches (web_master, web_config, web_maint) and
//  web_pass are device facts on purpose. They are set at the device
//  (ui/web_screen.cpp) and nowhere else. A backup that came through
//  the web interface must not be able to open the gates it came
//  through, nor lock its owner out of them.
// ============================================================

// Layout of the backup file, written into it as "format". A file with
// another number is not one of ours and is refused whole.
#define BACKUP_FORMAT   1

// NVS keys are at most 15 characters (16 with the terminator).
#define BACKUP_KEY_MAX  15

enum BackupKeyKind : uint8_t { BACKUP_SETTING, BACKUP_SECRET, BACKUP_DEVICE };

// The group a key belongs to. A null key is no setting of ours and answers
// BACKUP_DEVICE, the group that never travels.
BackupKeyKind backupKeyKind(const char* key);

// True if the key goes into a file written with or without secrets, comes
// back out of a file whose "secrets" field says the same, and, when a restore
// replaces the settings exactly, is one of the keys erased beforehand. One
// rule for the file, the import and the erase, so that they cannot disagree.
//   setting -> always, secret -> only with_secrets, device -> never.
bool backupKeyTravels(const char* key, bool with_secrets);

// A key that fits NVS: 1 .. BACKUP_KEY_MAX characters. Checked on every key
// read from a file before anything is written under that name.
bool backupKeyValid(const char* key);

// Binary values (a float is stored as four bytes) travel as hex text.
//
// Writes 2 * n lowercase hex digits and the terminating NUL, so out_size has
// to be at least 2 * n + 1. Returns false, and writes nothing, if it is not.
bool backupHexEncode(const uint8_t* data, size_t n, char* out, size_t out_size);

// Reads hex text of either case back into bytes and sets *out_n to how many.
// An even number of digits, at least two (a blob holds at least one byte),
// nothing but hex digits, and the result has to fit into out_size bytes.
// Returns false otherwise, and then writes neither out nor *out_n.
bool backupHexDecode(const char* hex, uint8_t* out, size_t out_size, size_t* out_n);
