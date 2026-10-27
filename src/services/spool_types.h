#pragma once

#include <stdint.h>

#include "services/spool_type_rules.h"   // SpoolType

// ============================================================
//  BAMBUDDY'S CATALOG OF EMPTY SPOOLS, AS THE SCALE READS AND ADDS TO IT
//
//  Bambuddy keeps a catalog of empty spools (GET and POST
//  /api/v1/inventory/catalog): a name and a weight in grams, 93 entries on
//  one instance. The scale offers it as "spool types" (spool_type_rules.h
//  holds the rules for choosing among them).
//
//  Only with Bambuddy in Spoolman mode (spoolTypesActive(); design note L16),
//  with one exception: reading the catalog also works in Bambuddy's own
//  inventory (spoolTypesCatalogActive()), for the card that makes a new spool
//  from a Bambu tag (V33). Everything else here answers "no" or does nothing
//  elsewhere.
//
//  The list is loaded when a screen opens and again after a type was added.
//  Names are kept whole (63 bytes), the weight in whole grams. The catalog's
//  own ids are not kept: a reset of the catalog on the server (/catalog/reset)
//  renumbers them, and the weight of a type goes to a spool by value
//  (core_weight), not by reference (design note L11).
//
//  These functions talk HTTP and block for up to their timeout. They are
//  called from the loop, never from an LVGL handler.
// ============================================================

#define SPOOL_TYPES_MAX  160   // places in the list; more entries on the server are left out

// True for Bambuddy in Spoolman mode, and only then. Every entry into the spool
// type screens asks this first, except one: the card for a new spool from a
// Bambu tag asks spoolTypesCatalogActive() below (V33).
bool spoolTypesActive();

// True for Bambuddy in either inventory mode: the catalog is Bambuddy's own
// table, the same path with or without Spoolman behind it, and the original
// reads it in Bambuddy's own inventory too (bambuddy_filament_db.cpp,
// BB_PATH_CORES in loadCores()). Only reading it and the list on top of it
// ask this; adding a type (spoolTypesCreate()) and every other entry into the
// spool type screens stay with spoolTypesActive() (V33).
bool spoolTypesCatalogActive();

// Fetches the catalog and replaces the list. Entries without a name or with a
// weight below one gram are left out. Returns the HTTP code (200 when the list
// was replaced), -1 when it is not asked for (spoolTypesCatalogActive() is
// false) or the request could not be built, -2 when the answer did not parse.
// A failed load leaves the list as it was.
int  spoolTypesLoad(uint32_t timeout_ms = 5000);

// How many types the list holds, in the order the server sent them.
int  spoolTypesCount();

// The list, spoolTypesCount() entries long. The entries change with the next
// load (the pointer stays), so a screen holds indices, not pointers to names.
const SpoolType* spoolTypesList();

// Adds a type to the catalog (POST) and reloads the list. The name is trimmed
// and cut like spoolTypeTrimName() does; whether it is taken already is for
// the caller to ask (spoolTypeNameTaken(), the server does not check). Returns
// 200 when the server took it, whatever the reload then did (it logs its own
// result); otherwise the HTTP code, or -1 when nothing was sent (not asked for,
// no name, no grams, no base url).
int  spoolTypesCreate(const char* name, int grams, uint32_t timeout_ms = 5000);

// What follows a tare in the display to say where it comes from: "" for the
// spool's own tare or none, " (Filament)" or " (Hersteller)" where the filament
// or the vendor has one. In Spoolman mode behind Bambuddy a tare from the
// filament level is " (geerbt)" instead: the proxy gives a spool with no weight
// of its own the inherited one, which may be the vendor's or a 250 g default
// rather than the filament's (mapSpool()). `source` is a TareSource.
const char* spoolTareNote(uint8_t source);
