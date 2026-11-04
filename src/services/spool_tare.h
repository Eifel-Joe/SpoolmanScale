#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

// The empty spool's weight, recorded at up to three levels: the spool's own,
// else the filament's default, else the vendor's. Only a level without a value
// passes on to the next; a stored 0 is a tare of nothing (tare_chain.h). With
// none set the tare is 0. The FilaMan mapper leaves a 0 of its own out, because
// there it means no value. Fork: the BamBuddy mapper keeps it, at the spool's
// level only when BamBuddy does not mark it inherited, because BamBuddy counts
// 0 as a real empty weight itself (spoolman_tare(), 1.3.1b1; Spec V70) - do not
// take the original's "core > 0" there on a later sync.
//
// One chain for every place that reads a spool: the lookup weighs with it,
// and the link and copy lists have to agree, or a copy built from a spool
// whose tare sits only on the filament or the vendor books the core as
// filament - 130 to 250 g on a 1 kg spool.
//
// `source`, when given, receives which level answered (TareSource in
// services/tare_chain.h).
float spoolTare(JsonVariantConst spool, uint8_t* source = nullptr);
