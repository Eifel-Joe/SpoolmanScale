#pragma once

#include <stdint.h>

// ============================================================
//  THE SIZE OF THE LABEL'S FACT LINES: THE RULE
//
//  V45 (Spec 5.3 Y, "docs/entwurf/2026-10-29-etikett-schrift.md"): the fact
//  lines of a label take the largest font in which all of them fit, in width
//  and in height, without making the code smaller. The renderer
//  (label_render.cpp, planLarger()) knows the fonts and measures; it hands
//  over the sizes it may use, in the order it prefers them, each with what
//  it measured. The choice among them is this rule:
//
//    pick    the first candidate whose font can spell every line, whose
//            widest line fits the width, whose lines fit the height, and
//            that leaves a code at least as large as the small lines did;
//            none of them: the small lines stay.
//
//  The rule lives here, without LVGL or Arduino: a host test holds it to the
//  values in test_label_fact_size.py, and the renderer asks this unit
//  instead of keeping the comparison of its own.
// ============================================================

// What the renderer measured for one candidate, in dots of the head.
//   width    the widest line, its bold stroke included; for two columns the
//            width both take side by side, the gap between them included
//   height   the lines stacked, the taller column for two
//   code     the edge of the code that stays next to or under the lines; 0
//            without a code, or where none was measured because the lines
//            do not fit
//   spells   whether the font has a glyph for every letter of the lines
struct LabelFactFit { int16_t width; int16_t height; int16_t code; bool spells; };

// The room the lines have, in dots: the width and height of the area, and
// the code the small lines left there (0: the label has no code).
struct LabelFactRoom { int16_t width; int16_t height; int16_t code_min; };

// The index of the first of the n candidates that spells, whose width and
// height are within the room's (equal fits) and whose code is no smaller
// than code_min; -1 when none is, or n is 0.
// Root: the sizes were a short fixed list (one line in 30, then the small
//   lines), so six facts two dots too wide for 30 printed in the small lines,
//   though 28 would have held them whole (Joerg's label, 29.10.2026).
// Fix: the renderer offers every size it has, in its order, and the first
//   one that passes all four tests wins.
// NOT-TO-DO: choosing among the passing candidates here, or sorting them.
//   The order is the renderer's: by the size the value prints in, large to
//   small, the two-line form first on equal size, so that a label with one
//   line fewer never prints smaller (Joerg 29.10.2026); the rule knows no
//   print sizes. Nor taking a width of 0 as a fit: it is how a letter the
//   font cannot spell is measured, and only `spells` says so.
// See test_label_fact_size.py.
int labelFactPick(const LabelFactFit* fits, int n, const LabelFactRoom& room);
