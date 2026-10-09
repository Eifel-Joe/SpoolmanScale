#pragma once

#include <ArduinoJson.h>

#include "services/ams_slots.h"

// ============================================================
//  THE PRINTER'S OWN AMS SHAPE
//
//  Bambu reports its AMS as ams[] of units, each with tray[], plus vt_tray[]
//  for the external holder and tray_now for the bay it feeds from. BamBuddy's
//  /printers/{id}/status passes that shape on, and so does an AMS source (see
//  ams_source.h). One parser for both, kept in bambuddy_api.cpp where it grew,
//  so neither path can drift from the other.
//
//  An AMS source adds what only it knows, all optional and absent on
//  BamBuddy: spool_id and remain_g per bay, gcode_state and mc_percent for
//  the printer.
// ============================================================

// The fields the parser reads, as an ArduinoJson filter.
void bambuStatusFilter(JsonDocument& filter);

// Fills out from one status answer and marks it valid. `who` names the
// server in the log lines.
void bambuStatusToAmsState(JsonObjectConst doc, int printer_id, AmsSlotState& out,
                           const char* who);
