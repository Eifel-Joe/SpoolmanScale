#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/klipper_store_rules.h"

// ============================================================
//  KLIPPER PRINTERS: THE STORE
//
//  V46/V48 (Spec 5.3 Z): the Klipper printers the scale knows are kept in the
//  settings memory (NVS), one record each, and their API keys together in one
//  entry of their own. The text of a record, the cleaning of what is typed,
//  the key list and the counting of room are rules of klipper_store_rules.h;
//  this unit is the memory around them.
//
//  NVS keys (at most 15 characters):
//
//    kl_p0 .. kl_p63   one printer each, a record as text (a setting)
//    kl_keys           the keys of all printers, "<id>=<key>;..." (a SECRET of
//                      the settings backup, web/backup_rules.cpp: the records
//                      hold no key, so a backup without secrets holds none)
//    kl_next           the next id to give out (a setting)
//    kl_loc            switch "storage location = printer name" (a setting)
//
//  All records are read once at boot into a cache and the cache answers every
//  read, so asking for the list of printers touches no flash. A write goes to
//  NVS first and into the cache only when it took.
//
//  THREADS: every function here is for the loop task only. NVS is flash and
//  prefs_store.h parks its writes while LVGL dispatches; the cache has no lock.
//  A task that needs a printer (the connection test, Task 6) is handed a copy
//  of the record and of the key and never calls in here.
// ============================================================

// What klipperStoreSave() found.
enum KlipperSave : uint8_t {
  KLS_OK = 0,
  KLS_BAD_NAME,       // nothing is left of the name after cleaning
  KLS_NAME_TAKEN,     // another printer has this name (the name is the storage location)
  KLS_BAD_HOST,       // no usable address (klipperHostClean)
  KLS_BAD_PORT,       // not returned here: the port is a uint16_t and 0 means the default;
                      // for the caller that parses the typed port (a number past 65535)
  KLS_BAD_KEY,        // characters a key may not have (klipperKeyClean)
  KLS_FULL,           // 64 printers, no free slot, no room in the settings memory,
                      // or the list of keys has no room for this key
  KLS_NOT_FOUND,      // an id that no printer has
  KLS_WRITE_FAILED    // the settings memory did not take a write
};

// Reads every record, the counter, the switch and the keys. Once at boot,
// beside amsSourceLoadSettings(), after the SD card is up so that the line
// lands in the log:
//   NVS: <used> of <total> entries used, <available> available, <n> Klipper printer(s)
// A record that does not decode is skipped and logged with its slot; its slot
// stays taken. Calling it again reads everything anew.
void klipperStoreLoad();

// The printers, ordered by id. The count is that of the printers that decoded.
int  klipperStoreCount();
bool klipperStoreAt(int index, KlipperPrinterCfg* out);          // false when index is out of range
bool klipperStoreFind(uint32_t id, KlipperPrinterCfg* out);      // false when no printer has this id

// Adds a printer (c->id == 0: a new id, the first free slot kl_pN) or changes
// one (c->id is the printer's). Name, host and key are cleaned by the rules of
// klipper_store_rules.h before anything is judged; a name another printer has
// is refused, port 0 becomes KL_PORT_DEFAULT. key == nullptr leaves the key of
// an existing printer as it is, "" takes it away; a new printer starts with
// the key it is given or none (a key left in the list under the same id is
// removed, it is never inherited). Changing the host or the port resets the outcome of
// the last test (KlipperTestOutcome{}, KT_NONE): it was about another address.
// c->last is not read, klipperStoreSetTest() is how the outcome is set.
// On KLS_OK *c holds the printer as it is now stored (id, cleaned texts, port,
// last); on any other result *c is left as it was. Nothing is stored then,
// with one exception: when the list of keys cannot be written after the
// record of an existing printer was (KLS_WRITE_FAILED), the printer is stored
// as asked and keeps its old key. A new printer in that case is taken back.
KlipperSave klipperStoreSave(KlipperPrinterCfg* c, const char* key);

// Removes a printer and its key. False when there is none with this id or a
// write did not take. The id is never given out again.
bool klipperStoreDelete(uint32_t id);

// Stores the outcome of a connection test with the printer. False when there is
// no such printer, a value is out of range (test past KT_BAD_ANSWER, layout past
// KL_LAYOUT_AFC) or the write did not take. An outcome the printer already has
// is not written again.
bool klipperStoreSetTest(uint32_t id, const KlipperTestOutcome& o);

// The key of a printer, "" and false when it has none or n is too small.
bool klipperStoreKey(uint32_t id, char* out, size_t n);

// How many more printers can be added, by the count and by the free entries of
// the settings memory as they are now (klipperRoomLeft).
int klipperStoreRoomLeft();

// "Storage location = printer name": whether a spool put into a Klipper
// printer is given the printer's name as its location in Spoolman. On from the
// factory.
bool klipperLocSwitch();
void klipperSetLocSwitch(bool on);
