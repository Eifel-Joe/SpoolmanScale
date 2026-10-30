#pragma once

#include <stdint.h>

#include "services/ams_slots.h"
#include "services/spool_color.h"

// ============================================================
//  AMS WEIGHTS
//
//  What a bay really holds, in grams, for the backends whose AMS
//  answer does not say.
//
//  FilaMan names the remaining grams per bay in the same answer
//  the grid is drawn from. BamBuddy does not: its printer status
//  carries the printer's own estimate in percent, while the real
//  figure - the one the scale itself weighed - sits in the
//  inventory behind it, one record per spool.
//
//  Fetching that while the view opens would put an assignment
//  request plus one request per bay in front of the first frame.
//  So the grid draws with what the printer said and this fills the
//  grams in afterwards, on a task of its own: same shape as the
//  drying batch, core 0, below the loop, one at a time. The loop
//  takes the result and redraws.
//
//  A Klipper printer names the spool of each field itself, in its
//  own answer, in both modes (Bambuddy's and Spoolman's): there is
//  no assignment list to read, the spool's record gives the grams.
//  The record also gives the colour and the material, which the
//  loop takes into the fields that have none (the printer reports
//  neither for a tool or for its one active spool).
// ============================================================

// Four units of four bays plus the external holder.
#define AMS_WEIGHTS_MAX  (AMS_MAX_UNITS * AMS_MAX_TRAYS + AMS_MAX_EXT)

enum AmsWeightsState : uint8_t {
  AWS_IDLE = 0,
  AWS_RUNNING,
  AWS_DONE
};

struct AmsWeightItem {
  int16_t grams;     // AMS_REMAIN_NA when the record has none
  uint8_t ams_id;
  uint8_t tray_id;
  // The spool's record: invalid and empty when it was not read. The loop takes
  // them only into a bay that has none of its own.
  SpoolColor color;
  char       name[AMS_NAME_MAX];   // the material
};

struct AmsWeightsResult {
  AmsWeightItem item[AMS_WEIGHTS_MAX];
  int           printer_id;
  uint8_t       count;    // bays answered
  uint8_t       found;    // of those, with a weight on file
};

// Starts a fetch for every bay of st that holds filament without a weight.
// False when nothing is missing, when a fetch is still running or uncollected,
// when the backend cannot resolve bays, or when the heap is too low.
bool amsWeightsStart(int printer_id, const AmsSlotState& st);

AmsWeightsState amsWeightsState();
bool            amsWeightsBusy();

// Valid while the state is AWS_DONE.
const AmsWeightsResult& amsWeightsResult();

// Hands the slot back. Only from AWS_DONE.
void amsWeightsTake();
