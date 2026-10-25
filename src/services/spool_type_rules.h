#pragma once

#include <stddef.h>

#include "app_config.h"   // NEW_SPOOL_TARE_MIN_G / NEW_SPOOL_TARE_MAX_G, defines only

// ============================================================
//  WHICH EMPTY SPOOL A READING POINTS AT: THE RULES
//
//  A spool type is an entry of Bambuddy's catalog of empty spools: a name
//  and a weight in grams ("Bambu Lab - Plastic Low Temp", 250). The catalog
//  has no field for the vendor. The vendor stands at the front of the name,
//  so "the types of a vendor" are the names that begin with it.
//
//  The weight of the chosen type goes to the one spool on the scale, as the
//  empty weight of that spool, and not to its filament or its vendor.
//  Bambuddy weighs with Spoolman's chain: the spool first, then the filament,
//  then the vendor, and the catalog is not part of it. A filament or a vendor
//  holds one empty weight only, so several types of one vendor have no other
//  place to go (design note 2.2, docs/entwurf/2026-10-25-leerspulen.md).
//
//  The rules live here, without Arduino, the backend or the screens: a host
//  test holds them to the values in test_spool_type_rules.py, and the screens
//  ask this unit instead of keeping rules of their own. The list is always a
//  parameter, so that the test can hand in its own.
//
//  Three numbers shape what the scale offers:
//
//    near     A reading points at the types of its vendor within
//             SPOOL_TYPE_NEAR_G (10 g) either side of it, the closest first
//             and at most SPOOL_TYPE_NEAR_MAX (5) of them.
//
//    same     An empty weight stored for a spool that lies within
//             SPOOL_TYPE_DEVIATION_G (5 g) of the one the reading gives
//             is the same spool, and more is another one.
//
//    unused   A spool with less than SPOOL_TYPE_UNUSED_G (0.5 g) consumed
//             counts as unused, which is the noise of a weighing and not a
//             use. Only an unused spool says anything about its own tare:
//             its filament is the label weight, so what the scale reads
//             above that is the spool.
//
//  What the scale asks first when a spool is put down (spoolTypeFirstAsk),
//  in this order, the first rule that fits decides:
//
//    1. The dish is empty (the bare reading is not above
//       NEW_SPOOL_TARE_MIN_G, the lightest tare a spool can have, or it is
//       not a number): STA_NONE.
//    2. The spool is not unused, or it has no label weight to take the tare
//       from: STA_NONE if it has a tare of its own, else STA_LIST (the types
//       of its vendor, nothing is measured).
//    3. The tare the reading gives is the bare reading minus the label
//       weight. If the spool has a tare of its own and that lies within
//       SPOOL_TYPE_DEVIATION_G (not more than 5 g) of it: STA_NONE.
//    4. Otherwise STA_SUGGEST if that tare is plausible (NEW_SPOOL_TARE_MIN_G
//       to NEW_SPOOL_TARE_MAX_G, both ends in), else STA_IMPLAUSIBLE.
// ============================================================

#define SPOOL_TYPE_NAME_MAX     64      // 63 bytes and the terminator
#define SPOOL_TYPE_NEAR_G       10.0f   // "near": this far either side of the reading
#define SPOOL_TYPE_NEAR_MAX     5       // "near": at most this many types
#define SPOOL_TYPE_DEVIATION_G  5.0f    // a stored tare this far off is the same spool
#define SPOOL_TYPE_UNUSED_G     0.5f    // less consumed than this counts as unused

struct SpoolType {
  char name[SPOOL_TYPE_NAME_MAX];   // the catalog name, vendor first
  int  grams;                       // whole grams
};

// What the scale asks when a spool is put down. STA_NONE: nothing to ask.
// STA_SUGGEST: a tare was read off an unused spool, offer the types near it.
// STA_IMPLAUSIBLE: the same, but the tare is no spool; say so and offer the
// list. STA_LIST: nothing can be read off this spool, show the list.
enum SpoolTypeAsk : unsigned char { STA_NONE = 0, STA_SUGGEST, STA_IMPLAUSIBLE, STA_LIST };

// True when the type name begins with the vendor, in any case (ASCII only)
// and with the blanks around both ignored, and the vendor ends where a word
// ends in the name: at its end, at a blank or at a dash. "Tin" is not
// "Tinmorry". Nothing belongs to an empty vendor, and a null name or vendor
// is no match. The name is read up to SPOOL_TYPE_NAME_MAX bytes.
bool spoolTypeOfVendor(const char* type_name, const char* vendor);

// The indices of the types of that vendor, in the order of the list. Writes
// at most out_max of them, never more, and returns how many (0 for no list,
// no out or a vendor that has none).
int spoolTypeFilter(const SpoolType* list, int n, const char* vendor, int* out, int out_max);

// The indices of the types within tol grams of the weight (exactly tol
// counts), the closest first, at most out_max of them: the nearest ones of
// the whole set, not the first ones of the list. At the same distance the
// order of the list decides. Only the types of that vendor are looked at,
// except that a vendor without any type in the list makes it all of them.
// Returns how many were written, never writes beyond out_max. A weight that
// is not a number, zero or negative, or a tolerance that is negative or not
// a number, gives 0.
int spoolTypeNear(const SpoolType* list, int n, const char* vendor, float grams, float tol,
                  int* out, int out_max);

// The tare a full spool on the scale gives: the bare reading minus the
// weight of the filament in it. Not cut; whether the result can be a spool
// is spoolTypeTarePlausible's question.
float spoolTypeTareFromFull(float bare, float fill);

// True for a tare of NEW_SPOOL_TARE_MIN_G to NEW_SPOOL_TARE_MAX_G, both ends
// in. Not a number is none.
bool spoolTypeTarePlausible(float tare);

// The weight a full spool is labelled with: the larger of the label weight
// and what is left, so that a spool filled over its label is not cut to it.
// Not a number, infinite and negative count as 0.
float spoolTypeFullLabel(float fill, float rest);

// What a full spool on the scale writes, in the whole grams the server keeps:
// the tare as it will be stored, the label weight, and a rest never above it.
struct FullSpoolWrite { int tare_g; int label_g; float rest_g; };

// The three writes of a full spool from the bare reading, the filament in it
// and the tare chosen or measured. The server stores the tare in whole grams
// and takes the rest as the reading less that stored tare, so the rest is
// worked out against the rounded tare (spoolTypeGrams), and a spool filled
// over its label lifts the label to the rest (spoolTypeFullLabel) before it
// is rounded. The rest is cut to the rounded label, so that it cannot round
// up past the label the server holds. A reading that is not a number or lies
// below the tare leaves a rest of 0.
FullSpoolWrite spoolTypeFullSpoolWrite(float bare, float fill, float tare);

// What to ask when a spool is put down: its label weight, the filament left,
// the bare reading (without the parts on the spool), whether the spool has a
// tare of its own and which one. The order of the rules is in the head of
// this file.
SpoolTypeAsk spoolTypeFirstAsk(float total, float remaining, float bare, bool own_tare, float stored_tare);

// src without the blanks on both sides, cut to n - 1 bytes and never in the
// middle of a UTF-8 character (a cut that would leave half of one drops the
// whole character). Writes the terminator even for an empty or null src; src
// may be dst. Nothing if dst is null or n is 0.
void spoolTypeTrimName(char* dst, size_t n, const char* src);

// True when the list has that name, in any case (ASCII only) and with the
// blanks around either ignored. A longer name is another one; an empty name
// or a null list is never taken. Names in the list are read up to
// SPOOL_TYPE_NAME_MAX bytes.
bool spoolTypeNameTaken(const SpoolType* list, int n, const char* name);

// Whole grams, rounded to the nearest. Not a number, infinite, zero and
// negative are 0; more than a million grams is a million, so that the cast
// to int cannot overflow.
int spoolTypeGrams(float g);
