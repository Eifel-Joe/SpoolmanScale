#pragma once

#include "services/label_layout.h"

// ============================================================
//  THE LABEL AT THE SCALE (Spec 5.3 AC f14, V75)
//
//  The label as it prints, on the left, next to its rows; a tap
//  on a row opens the keyboard of the scale's language. Three
//  ways in, each parked here and opened by labelEditTick():
//   - a spool's label from the print sheet ("Edit lines"): every
//     row changed for this one print only, forgotten afterwards;
//   - the template of the kind on the sheet ("Template"): the
//     switches of the browser's label page, the same line budget,
//     saved at once;
//   - a free label from the printer screen: head, band, title,
//     lines and a code with any text, no spool; kept until the
//     next restart.
//  After the idea of MitKIprogrammieren (SpoolmanScaleatPi
//  8254c8ed), built on our own template, renderer and keyboards.
//  The rows and the picture live in PSRAM.
// ============================================================

// Park the screen for the next loop pass.
void labelEditOpenSpool(LabelKind kind);
void labelEditOpenTemplate(LabelKind kind);
void labelEditOpenFree();

// From the loop (handlePrinterDeferredActions()): opens what was parked,
// prints, rebuilds after a change.
void labelEditTick();

// Closes the screen and its keyboard, for hideAllOverlays(); what a spool's
// label was changed to is forgotten, a free label is kept.
void labelEditClose();
bool labelEditOpen();
