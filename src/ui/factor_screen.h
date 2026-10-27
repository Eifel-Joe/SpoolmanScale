#pragma once

#include <stddef.h>

void showFactorScreen();
void buildFactorScreen();
// Deletes the screen and drops the labels the loop writes into. From the loop.
void closeFactorScreen();
// In the setup, back from the first step: frees the calibration and opens the
// step before it - the credentials in the browser, or the address. From the
// loop (cal_setup_back_pending).
void factorBackToSetupStep();
// The calibration's live reading for the loop to show: plain grams, or in
// the placing step a labelled line that tells raw counts from grams.
void factorLiveText(char *buf, size_t len, float grams);
