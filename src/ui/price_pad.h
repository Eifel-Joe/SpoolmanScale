#pragma once

// ============================================================
//  PRICE PAD
//
//  A keypad for a price, over everything else: digits, one point, two
//  decimals. Empty is a valid answer and means "no price". The currency is
//  the one the server keeps, so none is shown. What the price is, the title
//  says: what a spool cost for a new spool from the database (showPricePad),
//  a price per kg for a copy (showPricePadTitled, design note J, Festlegung
//  5). Built on demand and freed again; its own buttons only park their
//  answer, pricePadTick() hands it on and deletes the pad from the loop.
// ============================================================

// Opens the pad with `current` filled in (0: empty), titled "Price of the
// spool". `done` gets the price, 0 for an emptied field; on Cancel it is not
// called.
void showPricePad(float current, void (*done)(float price));

// The same pad under another title: title_id is a StringID, such as
// STR_PRICE_PAD_TITLE_KG for the copy card's price per kg. What the keys take
// and what `done` gets are the same.
void showPricePadTitled(float current, int title_id, void (*done)(float price));

// The same pad for another quantity than a price (the density on the card for
// a new filament, V34): the title and the grey line beside the keys are
// StringIDs too, since the line for a price talks about the currency and the
// server. What the keys take (digits, one point, two decimals) and what `done`
// gets are the same; the titled pad above shows the price's line.
void showPricePadInfo(float current, int title_id, int info_id, void (*done)(float value));

bool pricePadOpen();
void closePricePad();

// From the loop: carries out OK or Cancel, which the pad's buttons parked.
void pricePadTick();
