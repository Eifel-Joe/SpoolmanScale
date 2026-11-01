#pragma once

#include <WebServer.h>

// SSC_ACCEPTANCE comes from here when the documentation firmware sets only
// SSC_DOC_BUILD: without this include the #if below would be false and that
// firmware would silently lack the remote control.
#include "app_config.h"

// The routes of the acceptance and documentation builds (SSC_ACCEPTANCE, never
// in a release; services/doc_build_rules.h), behind the maintenance gate like
// the screenshots they serve. They are listed in web_doc.cpp, the one file that
// spells them. Registered by the logs page next to the screenshot routes.
//
// A release gets the empty stub of the #else branch, so the logs page calls
// this without a switch of its own and a release holds no such route.
#if defined(SSC_ACCEPTANCE)
void docRoutes(WebServer& srv);
#else
static inline void docRoutes(WebServer&) {}
#endif
