#pragma once

// A row of the copy list and of the link list: the spool id at the left, the
// name behind it on the same line, both in the 16 px font.
//
// Wurzel: the name began at 50 whatever the id, which held four digits (at
// most 44 px from 6); five take up to 55 px at LVGL's per-glyph rounding
// ("40000"), and the id ran into the name.
// Fix: the name begins behind the id that is there, never left of where it
// always began, so a row with a short id looks as it did.
// NOT-TO-DO: moving every name to the right of five digits: every row would
// lose 15 px of name for ids almost nobody has. See test_spool_id_width.py.
constexpr int LIST_ROW_ID_X     = 6;    // the id, from the row's left
constexpr int LIST_ROW_NAME_X   = 50;   // where the name begins at the least
constexpr int LIST_ROW_ID_GAP   = 4;    // between the id and the name
constexpr int LIST_ROW_TEXT_END = 446;  // where the name ends, 6 px inside the row

// Where the name begins beside an id `id_w` pixels wide.
inline int listRowNameX(int id_w) {
  int x = LIST_ROW_ID_X + id_w + LIST_ROW_ID_GAP;
  return x > LIST_ROW_NAME_X ? x : LIST_ROW_NAME_X;
}
