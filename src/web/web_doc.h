#pragma once

#include <WebServer.h>

// The routes of the documentation build (services/doc_build_rules.h), behind
// the maintenance gate like the screenshots they serve:
//   POST /api/touch?x=&y=[&ms=][&x2=&y2=]  touch the display (480x320 coordinates)
//   POST /api/sim/weight?g=<grams>|off      a simulated weight on the pad
// Registered by the logs page next to the screenshot routes.
void docRoutes(WebServer& srv);
