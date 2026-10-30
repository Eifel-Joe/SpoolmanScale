#pragma once

// The loop's side of the Klipper page's connection test. A finished test must
// land in the store whether or not a browser comes back to ask for it, so the
// loop calls this once per pass; the page's own routes and starts store a
// finished result through the same function (page_klipper.cpp).
//
// Loop task only, like the store it writes to.
void klipperTestTick();
