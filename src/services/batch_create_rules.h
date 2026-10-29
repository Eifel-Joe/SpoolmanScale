#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  SEVERAL IDENTICAL SPOOLS IN ONE GO: THE RULES
//
//  n10 (V44, "docs/entwurf/2026-10-28-beta6-bedienung.md", part 4): "Create
//  spool" without a tag (X) can make several spools that are alike, from one
//  choice of source, spool type, fill weight and price. The screens make them
//  one after the other, each with its own call to the server, and tell the
//  user afterwards which numbers came out. The pieces that are decisions and
//  not screens are here:
//
//    count   What the number pad may hand over: a whole number from
//            BATCH_COUNT_MIN to BATCH_COUNT_MAX.
//    step    What happens after each try: the next spool, the end of the run,
//            or a stop at a failure (the spools made stay made, the scale
//            deletes nothing).
//    ids     The numbers of the spools made as one short text, for the
//            overview ("41, 43-45") and for the message after a stop.
//
//  The rules live here, without Arduino, the backend or the screens: a host
//  test holds them to the values in test_batch_create_rules.py, and the
//  screens ask this unit instead of keeping rules of their own.
// ============================================================

#define BATCH_COUNT_MIN        1    // one spool is no batch, but it is the same path
#define BATCH_COUNT_MAX       10    // the most in one go
// The text of the ids with its terminator. Ten ids of up to seven digits, none
// next to another, are 10 * 7 + 9 * 2 = 88 bytes, so the widest case fits with
// room; a spool number has far fewer digits than that.
#define BATCH_IDS_TEXT_MAX    96

// Whether a typed count is valid: a whole number from BATCH_COUNT_MIN to
// BATCH_COUNT_MAX, both ends in. The number pad hands the value over as a
// float, so 2.5 and 0.9 are refused rather than cut, and a value that is not a
// number (NaN) or not finite is not valid.
bool batchCountValid(float typed);

// What follows one try to make a spool.
//   BS_NEXT          go on with the next spool
//   BS_DONE          that was the last one wanted
//   BS_STOP_FAILED   the try failed and is known to have failed
//   BS_STOP_UNSURE   the try failed in a way that may still have made the spool
//                    (the answer got lost): the user is sent to look in stock
// Wurzel: with one call to the server per spool, a failure in the middle leaves
// some spools made and some not, and the user has to be told which.
// Fix: the first failure stops the run; what was made stays made and is
// reported (the scale deletes nothing).
// NOT-TO-DO: retrying the failed spool here. After a lost answer the spool may
// exist already, and a second try makes it twice.
// See test_batch_create_rules.py (the steps after a failure, also the first).
enum BatchStep : uint8_t { BS_NEXT = 0, BS_DONE, BS_STOP_FAILED, BS_STOP_UNSURE };

// The step after one try. `made` counts the spools made so far, the one that
// has just worked included; `wanted` is the count typed in. A failed try
// (ok false) stops the run, as "unsure" when it may have made the spool and as
// "failed" otherwise; `unsure` means nothing when the try worked.
BatchStep batchCreateStep(int made, int wanted, bool ok, bool unsure);

// The ids of the spools made, in the order made, as the text for the screen:
// separated by ", ", and a run of three or more numbers that each follow the
// last by one as a range with an ASCII hyphen and no blanks ("41-45",
// "41, 43-45"; two in a row stay "41, 42"). Descending numbers are no run.
// Ids are spool numbers, one or more; they are not checked. False, with `out`
// left empty, when n is below 1 or `cap` has no room for the text and its
// terminator (BATCH_IDS_TEXT_MAX is enough for up to ten ids).
bool batchIdsText(const int* ids, int n, char* out, size_t cap);
