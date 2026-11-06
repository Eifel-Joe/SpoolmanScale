#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/brother_ql_rules.h"

// ============================================================
//  WHICH ROLL IS IN THE BROTHER: THE RULES (Spec 5.3 L1, V78)
//
//  Before every label the scale asks a QL-810W/820NWB which roll
//  it holds, and sends nothing when the printer would not print
//  it anyway: another roll than the one set, none, an open cover,
//  a roll at its end or an error (v0.8.7-beta.8).
//
//  The way. The raw port 9100 answers no status request: Joerg's
//  QL-820NWBc sent 0 bytes to ESC i S twice (06.11.2026), the
//  reference shows the request in its USB flows only (R 5.1-5.4,
//  5.9 "simply sent as is"), and thermal-label/brother-ql found
//  the same on the same model. SNMP does answer, in 1 to 15 ms:
//  Brother's own status block under one OID (Brother developer
//  FAQ: "the same as the return value of the status request
//  information"; QL-810W, QL-820NWB, QL-1110NWB), the 32 bytes of
//  R p.23. The QL-700 behind a forwarder has no SNMP and is not
//  asked (label_printer.cpp).
//
//  Why a job on the wrong roll never prints: every job of the
//  scale sets the media type, width and length valid (0xCE, R
//  p.32), and then "If the media is not correctly loaded ... an
//  error status is returned" - the red lamp of 06.11.2026.
//
//  The request is SNMP v1, community "public" (Brother's default),
//  one GET. The answer is taken only when it is the GetResponse to
//  that request number, without error, for that OID, with a block
//  that starts 80 20 42 34; anything else counts as no answer, and
//  no answer prints as before (V78: nothing may hang on the check).
//
//  Here without Arduino and without the network, so that
//  test_brother_ql_status.py can hold the bytes to the ones Joerg's
//  printer answered. The socket is services/brother_ql_snmp.cpp.
//
//  Sources:
//    R        Brother "Raster Command Reference QL-800/810W/820NWB"
//             v1.01: p.23 (the 32 bytes), p.24 (error information
//             1 and 2), p.25 (width and length), p.26 (media type,
//             status type), p.32 (ESC i z and the error it raises).
//    FAQ      https://support.brother.com/g/s/es/dev/en/command/faq/
//    befunde  docs/recherche/2026-11-06-v087-beta8/statusanfrage/
//             befunde.md (the answers of Joerg's QL-820NWBc).
//    bql      pklaus/brother_ql reader.py: RESP_MEDIA_TYPES 00, 0A, 0B.
// ============================================================

#define BROTHER_QL_SNMP_PORT       161
#define BROTHER_QL_STATUS_BYTES    32
// Two tries of half a second: Joerg's printer answers in 1 to 15 ms, and a
// lost first datagram gets a second chance; at most 1 s for one that does
// not answer at all (Spec V78).
#define BROTHER_QL_STATUS_TRIES    2
#define BROTHER_QL_STATUS_WAIT_MS  500
// A request is 46 bytes with a request number below 128, 48 at most.
#define BROTHER_QL_SNMP_REQUEST_MAX 48
// An answer is 78 bytes; room for an agent that says more.
#define BROTHER_QL_SNMP_ANSWER_MAX  128

// What the roll byte says (R p.26 writes 4A/4B, the print information and
// brother_ql 0A/0B, and Joerg's printer sends 0B: both are taken).
enum BrotherQlMedia : uint8_t {
  BQS_NONE = 0,      // 00: no roll
  BQS_CONTINUOUS,    // 0A or 4A
  BQS_DIE_CUT,       // 0B or 4B
  BQS_OTHER,         // a byte the scale does not know
};

// The parts of the block the scale uses.
struct BrotherQlStatus {
  uint8_t err1;       // error information 1 (R p.24)
  uint8_t err2;       // error information 2
  uint8_t width_mm;   // the roll's width; 0 without one
  uint8_t length_mm;  // die-cut: the label's length; continuous: 0
  uint8_t media;      // BrotherQlMedia
  uint8_t model;      // model code, 0x41 for a QL-820NWB (log only)
};

// The GET of Brother's status OID with this request number into `out`;
// its length, 0 and nothing written when `cap` does not hold it.
size_t brotherQlStatusRequest(uint16_t request_id, uint8_t* out, size_t cap);

// Reads an answer: true and `out` filled only for the GetResponse to
// `request_id` that carries the block (see above). Nothing is read past `n`.
bool brotherQlStatusParse(const uint8_t* buf, size_t n, uint16_t request_id,
                          BrotherQlStatus* out);

// What the scale makes of the block against the roll set.
enum BrotherQlRollVerdict : uint8_t {
  BQV_OK = 0,      // the roll that is set, nothing wrong: print
  BQV_UNCHECKED,   // a media byte the scale does not know: print as before
  BQV_OTHER,       // another roll the scale knows ("switch to" it)
  BQV_UNKNOWN,     // a roll the scale cannot print on
  BQV_NO_ROLL,
  BQV_COVER,       // the cover is open
  BQV_ROLL_END,    // the roll is used up or cannot be fed
  BQV_ERROR,       // cutter jam, printer off, replace media, buffer, line, system
};

// In this order: an open cover first (the roll is read again once it is
// shut), then no roll, the end of the roll, the roll itself (another roll
// says more than the "replace media" a job on it left), the other errors.
// "Printer in use" and the bits Brother marks as not used are no error.
BrotherQlRollVerdict brotherQlRollVerdict(const BrotherQlStatus& s, const BrotherQlLabel& set);

// The table's entry for the roll in the printer; nullptr when the table has
// none like it (or there is no roll).
const BrotherQlLabel* brotherQlStatusLabel(const BrotherQlStatus& s);
