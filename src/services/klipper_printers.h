#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/ams_slots.h"
#include "services/klipper_rules.h"
#include "services/klipper_store_rules.h"

// ============================================================
//  KLIPPER PRINTERS IN THE AMS VIEW: THE GLUE
//
//  V46/V48 (Spec 5.3 Z, "docs/entwurf/2026-10-29-z-klipper.md"): the stored
//  Klipper printers (klipper_store) become entries of the printer list of the
//  AMS view, their facts (moonraker_api, klipper_rules) become units and bays,
//  and the assignment of a spool to a bay becomes the commands of the plan
//  with a read-back. backend_api.cpp only switches on the printer number
//  (klipperIsPrinterId); everything about a Klipper printer is here.
//
//  THREADS: the loop task only, like the store. The connection test (a task of
//  its own) calls moonrakerProbe() and nothing in here.
// ============================================================

// Whether the scale has a Spoolman the Klipper printers can share: its own
// (Spoolman mode), or the one behind a Bambuddy that keeps its inventory there
// (Bambuddy in Spoolman mode with the address known). A spool number means the
// spool of one Spoolman, so without it there is nothing to assign.
bool klipperAvailable();

// The scale's Spoolman, the one the printers must talk to as well
// (backendFdbBaseUrl()); "" when klipperAvailable() is false.
const char* klipperScaleSpoolman();

// A printer number of the list belongs to a Klipper printer from KL_ID_BASE on.
bool klipperIsPrinterId(int id);

// A Klipper printer is stored and usable: klipperAvailable() and at least one
// in the store.
bool klipperHasPrinters();

// Adds the stored printers to the list, ordered by their numbers, behind what
// the backend put there, up to AMS_MAX_PRINTERS (the rest is cut with a line
// in the log). The entries carry kind, the layout and the number of fields of
// the printer's last connection test.
void klipperAppendPrinters(AmsPrinterList& out);

// Reads the printer and fills the units and bays: AMS_MODEL_KLIPPER, the unit's
// name as label (empty for the one active spool), the bay's spool, colour and
// material, the loaded field as active, the print state and progress. 200 when
// it was read; the code of the request that failed, and KL_E_NO_PRINTER for
// a number that is none. The facts stay until the next call, for
// klipperFieldName() and klipperTruncated().
int klipperGetAmsState(int printer_id, AmsSlotState& out);

// Puts the spool into the bay (ams_id is the unit, tray_id the place in it):
// 200 only when a second read of the printer shows the field holds it.
//   KL_E_OTHER_SPOOLMAN  the stored test says the printer talks to another
//                        Spoolman: no command is sent
//   KL_E_NO_PRINTER      KL_E_NO_FIELD   no such printer, no such bay
//   KL_E_NOT_HELD        the commands ran, the printer does not hold the spool
//   other                the code of the request that failed
// After a 200 the spool's location becomes the printer's name, when the
// setting is on and the printer does not write it itself; the answer of that
// write goes to the log and changes nothing.
int klipperAssign(int spool_id, int printer_id, int ams_id, int tray_id);

// The name of a bay as the printer calls it (Happy Hare "MMU 3", an AFC lane's
// name, a tool "T1") from the facts of the last klipperGetAmsState() of this
// printer. False when the bay has no name of its own (the one active spool) or
// the facts are those of another printer: the view puts its own word.
bool klipperFieldName(int printer_id, int ams_id, int tray_id, char* out, size_t n);

// The printer's name, "" for a number that is none. The text lives until the
// next call.
const char* klipperPrinterName(int printer_id);

// The last klipperGetAmsState() of this printer found more fields than the
// scale shows (KL_FIELDS_MAX).
bool klipperTruncated(int printer_id);
