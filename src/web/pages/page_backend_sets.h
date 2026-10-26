#pragma once

#include <Arduino.h>
#include <WebServer.h>

// The card on the backend page that shows the active and the parked server
// set side by side and switches between them (services/server_sets.h), and
// the routes behind it. Shown in every mode: a set holds all three backends.
// Without a parked set it is one line that offers to set one up.
String backendSetsCard();
void   backendSetsRoutes(WebServer &srv);
