#pragma once

#include <stdint.h>

#include "tag_create.h"

// ============================================================
//  SPOOLMAN: THE FILAMENT FOR A TAG
//
//  Finds the filament a tag describes, and creates it (and its vendor) when
//  the inventory has none. Spoolman's own filament database (SpoolmanDB,
//  /api/v1/external, since 0.27 searchable on the server) supplies density,
//  temperatures and empty spool weight; without an entry there the tag's
//  input fills the filament, and the density is the one the database's
//  filaments of that material share most, else Spoolman's material list's.
//  Called through backend_api.h only.
// ============================================================

// Reads only. Fills plan with what creating the spool would take: TFS_FOUND with the
// filament's id, TFS_CREATE_DB, TFS_CREATE_TAG, TFS_NEEDS_CATALOG, or
// TFS_FAILED when the server could not be asked.
//
// bambuddy_names: BamBuddy sits in front of this Spoolman. It reads a
// filament's line out of its name and writes the name anew as "<material>
// <line>" on every spool edit (services/bambuddy_name.h), so a filament the
// scale creates is named that way from the start, and a typed sort is found
// again by that name and by its colour (the name does not tell colours apart),
// a pick from the database by its external_id. False for Spoolman used
// directly: the names stay as they are there (SpoolmanDB's style, the line
// then the colour).
void spoolmanPlanTagFilament(const char* base_url, const TagCreateInput& in, TagFilamentPlan* plan,
                             bool bambuddy_names);

// Creates the vendor when plan has none, then the filament. Returns the HTTP
// code of the last request, out_filament_id the new filament.
int spoolmanCreateTagFilament(const char* base_url, const TagCreateInput& in,
                              const TagFilamentPlan& plan, int* out_filament_id);
