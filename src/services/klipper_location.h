#pragma once

// ============================================================
//  THE OLD LOCATION OF A SPOOL IN A KLIPPER PRINTER: SPOOLMAN
//
//  V50 (Spec 5.3 Z, the bullet on the location, its part on the old one):
//  after a Klipper assignment the spool's location in Spoolman becomes the
//  printer's name, and its location before that is kept in the spool's extra
//  field scale_prev_location. The spool that stood in the field before gets
//  that location back. What is remembered and what the displaced spool gets
//  are the rules of klipper_location_rules.h; this unit is the requests
//  around them.
//
//  Everything goes directly to the scale's Spoolman (klipperScaleSpoolman()),
//  in Bambuddy mode as well: Bambuddy's proxy carries no extra field.
//
//  THREADS: the loop task only, like klipper_printers (klipperAssign() calls it).
// ============================================================

// Called by klipperAssign() once the read-back proved the printer holds
// spool_id, and only where the scale writes the location
// (klipperWritesLocation()). The spool's location becomes printer_name and, in
// the same PATCH, its old location is remembered (klipperPrevToRemember()).
// Spoolman that does not know the field answers 400: the field is made and the
// PATCH sent once more. displaced_id is the spool that stood in the field
// before the commands (0, or spool_id itself, for none); it is read from
// Spoolman and gets klipperRestoreFor(). When the field cannot be made or the
// PATCH with it fails, the location is written alone and the displaced spool's
// location is only cleared, and only while it is the printer's name. When
// spool_id cannot be read or its old location does not fit, the location is
// written alone too, but the displaced spool still gets its old one back.
// When Spoolman does not answer the first read at all, nothing more is sent.
// True when spool_id's location was written (with or without the old one).
// Each failure is one "KLIPPER:" line in the log and nothing else: the
// assignment has succeeded already.
bool klipperLocationAssigned(int spool_id, int displaced_id, const char* printer_name);
