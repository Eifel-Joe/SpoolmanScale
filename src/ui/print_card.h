#pragma once

#include "services/label_printer.h"

// ============================================================
//  THE PRINT CARD
//
//  One card for a print from start to answer. While the session runs it
//  says where it stands - preparing, looking for the printer, connecting,
//  sending, printing - and then turns into the answer in the same place:
//  confirmed by the printer, sent without a confirmation (a Brother's
//  "sent" among them), or not printed and why. A confirmed print closes
//  on its own after a few seconds, a Brother's "sent" after 2 s; a problem
//  waits for OK.
//
//  The session blocks the loop, so while it runs the card is drawn by
//  printCardTick(), which the session calls as its progress callback.
//
//  One answer comes later and on its own: what a Brother says in the 10 s
//  its line is held after the label (printer_screen.cpp, brother_ql.h). It
//  takes the place of an answer still on screen, and waits for OK.
// ============================================================

// Opens the card in its busy state, "preparing the label".
void printCardShow();

// The session's progress callback: follows labelPrinterSessionPhase() and
// redraws, at most every few tens of milliseconds.
void printCardTick();

// The answer, after printCardShow() or on its own (a Brother's reply), in
// place of an answer still shown. text_id is labelPrintResultString(result)
// for a failure. detail: the server's own line under it, where it gave one,
// or the bytes a Brother sent.
// offer_own_layout: a second button that prints the same spool with the
// scale's own layout (sets print_own_layout_pending).
void printCardResult(LabelPrintResult result, const char* detail = nullptr,
                     bool offer_own_layout = false);

bool printCardOpen();

// The label's place in a run of several (batch_create.h): every card built
// from now on shows "Label 2 of 5" in its top right corner; n below 2 shows
// none, as before. Set before the print, back to (0, 0) after it.
void printCardSetStep(int i, int n);

// From appLoop(): closes the card when OK or the countdown asked for it,
// never from the card's own callback.
void printCardLoop();
