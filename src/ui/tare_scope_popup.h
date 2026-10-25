#pragma once

// Where an empty spool weight is stored: spool, filament or brand. measured
// is false for a typed value, which drops the hint about a bag on the pad.
// then_new_spool: the New spool button derived this tare, so the initial
// weight follows once it is stored.
void showSpoolWeightScope(float grams, bool measured, bool then_new_spool = false);
// That question is up. A modal like the rest: the value in it is about to
// change the tare, and an automatic weighing meanwhile would use the old one.
bool isSpoolWeightScopeOpen();
// Closed, not hidden, without parking anything: for the navigation.
void closeSpoolWeightScope();
