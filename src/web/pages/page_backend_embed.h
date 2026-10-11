#pragma once

#include <Arduino.h>

// The card at the foot of the backend page that explains how to show this
// web interface inside FilaMan (through its plugin) or BamBuddy (through a
// sidebar link). Empty in Spoolman mode and without a backend address.
String backendEmbedCard();
