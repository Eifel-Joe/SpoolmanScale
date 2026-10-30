#pragma once

struct AmsPrinterList;

// ============================================================
//  THE WINDOW "CHOOSE PRINTER" OVER THE AMS VIEW (V47)
//
//  A full screen with one button per printer in two columns, in the order
//  the list arrives: the name above, under it the printer's kind (the Bambu
//  model, or Klipper with its layout and the number of its tools or slots,
//  services/printer_pick_rules.h). A printer without a kind (FilaMan, the AMS
//  source) has its name alone, in the middle of the button. The current
//  printer carries the accent border and a tick, and the list scrolls to it.
//  Under the list "Cancel" in the style of the original, in the header the
//  back arrow, the magnifier and the close button.
//
//  The magnifier is the one of the colour lists (ui/colour_search.h, V43),
//  with texts of its own: a word keeps the printers whose name or kind holds
//  it (printerPickShows()), says so when none does, and the tick stays only
//  where the current printer is among them. Back ends a search first and the
//  window stays; the x of the word does the same. A list cut for memory ends
//  in a line that points to the magnifier, which builds the hits only and so
//  finds the rest.
//
//  The window shows the list the caller has loaded and asks for nothing. It
//  keeps the pointer: the list must stay as it is while the window is open,
//  and the caller closes the window before it changes or frees the list.
//
//  Buttons and the back arrow only park the tap; printerListTick() closes the
//  window and then calls back, from the loop: on_pick with the id of the
//  printer tapped (the current one included), on_leave for Cancel and for back
//  without a word. The close button of the header goes through
//  showMainScreen(), whose path has to reach closePrinterList(): that one
//  calls nothing back.
// ============================================================

// A null list opens nothing: logged, and on_leave is called at once, so a
// caller that hid its own screen for the window shows it again.
void showPrinterList(const AmsPrinterList* list, int current_id, void (*on_pick)(int printer_id),
                     void (*on_leave)());
bool isPrinterListOpen();
void closePrinterList();   // no callback
void printerListTick();    // loop: the search, the parked tap
