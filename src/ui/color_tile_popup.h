#pragma once

#include <lvgl.h>

#include "services/spool_color.h"

// The colour of a spool in large, opened by a tap on its swatch: the name the
// screen shows, the colour across most of the card, its code underneath. A
// gradient or dual colour spool shows both colours, a clear one the glass
// fade the small swatches use. Closed by its X or a tap anywhere on it.
struct ColorTileSpec {
  SpoolColor  color;
  SpoolColor  second;     // a two colour spool's other colour, invalid for one colour
  const char* name;       // may be empty or null: the code then stands in the title
};

// Nothing opens for a colour that is not known, so a grey placeholder swatch
// stays inert.
void showColorTile(const ColorTileSpec& spec);

// Makes `swatch` open the tile when tapped. The colour is asked for at the
// moment of the tap, because the swatch on the main screen is repainted on
// every scan while the object stays the same. `extra_px` widens the touch
// area beyond the drawn square, which is only 42 px.
typedef ColorTileSpec (*ColorTileSource)();
void colorTileAttach(lv_obj_t* swatch, ColorTileSource source, int extra_px);
