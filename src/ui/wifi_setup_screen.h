#pragma once

// WPA2 allows 63 characters, or 64 as hex. The textarea stops there, so a
// longer entry is visibly refused instead of silently cut on the copy.
#define WIFI_PASS_MAX_LEN   64

void showWifiSetupScreen();
// Runs the scan and the connect attempt the setup screens parked. From appLoop().
void handleWifiSetupDeferredActions();
void buildWifiSetupScreen();
void doWifiScan();
void showWifiPassScreen();
void buildWifiPassScreen();
void showWifiConnectingScreen();
// The result screen for a link that is already up: no connect attempt, with
// a button to pick a different network instead of Retry.
void showWifiConnectedScreen();
void buildWifiConnectingScreen();
// Deletes the connecting screen and drops the label pointers the connect
// result writes, so a later result cannot reach a screen that is gone.
void closeWifiConnectingScreen();
// Connect as if ssid and pass had been typed on the password screen: stored,
// tried, result on the connecting screen. Runs on the next loop pass.
void wifiSetupConnectWith(const char *ssid, const char *pass);

// The network picked on the list, for the password and result screens.
const char *wifiSetupChosenSsid();
// The password screen hands its text over here; it is stored and tried on
// the next loop pass, not inside the keyboard's callback.
void wifiSetupSubmitPassword(const char *pass);
