#pragma once

#include <stdint.h>

// ============================================================
//  A COPY BEHIND BAMBUDDY: READ THE TEMPLATE, CREATE THE SPOOL
//
//  The rules of what goes along, the empty spool's (L21) among them, are in
//  copy_rules.h; this unit reads, holds and sends.
//
//  Behind Bambuddy with its spools on a Spoolman server (spoolTypesActive(),
//  "BBS") a copy made at the scale carries all of its template's fields: the
//  Spoolman filament it hangs on, the profile and its name, the profiles per
//  printer model, the price per kg, the storage location, the suppliers and
//  the calibration values (K profiles). Weights and the label come from the
//  scale; the note and the dates stay behind, they tell of the template.
//
//  With Bambuddy's own inventory, copyBbsCreate() sends what the copy always
//  sent (material, subtype, brand, colour name, colour, label weight, empty
//  spool, consumption), read fresh from the template, and nothing more.
//
//  The order of calls, all from the loop (each read holds it up to 3 s):
//
//    copyBbsLoad(id)        before the card: the template as Bambuddy stores
//                           it, its profiles per printer model and (BBS) its
//                           filament id and weight on the Spoolman server.
//                           Held in PSRAM until the next load or
//                           copyBbsClear(); a load also forgets a price typed
//                           before.
//    copyBbsNominal(l)      the nominal weight the card and the create start
//                           from: the filament's, else the label l.
//    copyBbsView(&v)        what the card shows, from what was loaded.
//    copyBbsSetPrice(p)     the price button, when it was used: a price per kg.
//    copyBbsCreate(in, ...) "Create": through backendCreateSpool(), the POST
//                           with every field, then the link lists.
//    copyBbsLastMissing()   what did not come across (CopyMissing bits).
//    copyBbsClear()         after the create, or when the card is cancelled.
//
//  Why the filament id is read at Spoolman. Wurzel: Bambuddy's answer for a
//  spool carries no filament id, and a create without one makes Bambuddy
//  look for a filament by material, subtype, brand, colour and label weight,
//  or build one: the copy can land on a look-alike or on a new filament, and
//  with it lose the temperatures and the article number. Fix:
//  the Spoolman server behind Bambuddy names filament.id for the same spool
//  id, and the create sends it as spoolman_filament_id. When it cannot be
//  read, the copy is made without it as before and reports CM_FILAMENT.
//  NOT-TO-DO: matching a filament by its fields at the scale is the same
//  guess Bambuddy makes; failing the copy for want of the id would block the
//  copy whenever Spoolman is out of reach, where the old way still works.
//
//  Why the link lists go out only when the template has entries. Wurzel:
//  Bambuddy replaces a spool's profiles per model, suppliers and K profiles
//  as a whole list, and a newer Bambuddy fills them from per-product defaults
//  when it creates a spool. Fix: a list goes out after the create only when
//  the template has one, and then complete. NOT-TO-DO: sending "[]" for a
//  template without entries, to make the copy equal its template, would wipe
//  the defaults Bambuddy has just given the new spool.
//
//  Why the values read before the card win. Wurzel: the card shows what the
//  copy takes over, and a second read at "Create" (the old way) could bring
//  other values than the card showed, at the cost of another stall.
//  Fix: the create uses what was loaded for the same template id and reads
//  only when nothing was loaded for it, so the create never depends on the
//  card having run. NOT-TO-DO: using a load whatever
//  its id - a load left over from another template would dress the copy in
//  that template's profile, price and suppliers.
// ============================================================

// What the card shows. The strings point into the loaded template and stay
// valid until the next copyBbsLoad(), copyBbsClear() or a copyBbsCreate()
// for another template, which loads that one. Each is "" (never null) where
// the template has nothing, and the card shows a dash for it.
struct CopyBbsView {
  const char* profile_name;   // "" without a profile id (copyRulesHasProfile)
  int         model_presets;  // profiles per printer model, 0 when none or unreadable
  const char* location;       // the template's storage location by name
  const char* supplier;       // the purchase source, else the first supplier
  float       cost_per_kg;    // the template's price per kg, 0 without a price
};

// What the copy is created with. initial_g is the copy's label weight behind
// Spoolman (the start rule, spool_start.h: a full copy gets what it weighs,
// else the nominal weight of copyBbsNominal()) and in Bambuddy's own inventory
// only the stand-in for a template without a label; remaining_g what is on
// it. chosen_core_g is a spool type chosen at the scale for the copy, 0 for
// none; tare_g the empty spool to fall back on for a template without
// core_weight (L21).
struct CopyBbsCreate {
  int   template_spool_id;
  float initial_g, remaining_g, chosen_core_g, tare_g;
};

// Reads the template (raw, as Bambuddy stores it), its profiles per printer
// model and, behind Spoolman, its filament id and weight, each within 3 s,
// and holds them until the next load or copyBbsClear(). False only when the template
// itself could not be read; profiles or a filament id that could not be read
// leave a gap the create reports. Forgets a price typed before.
bool copyBbsLoad(int template_spool_id);

// The nominal weight the copy of template_spool_id starts from
// (copyRulesNominal): the Spoolman filament's weight loaded for that very
// template, else tpl_label_g, the template's label, which is also what comes
// back when nothing or another template is loaded.
float copyBbsNominal(int template_spool_id, float tpl_label_g);

// Fills out with what the card shows; cost_per_kg is the template's price per
// kg as Bambuddy keeps it (0 where it has none, or none that is a finite
// price above 0), not a price typed since: the card keeps that one on its
// button. The label weight plays no part, a price per kg is the same for any
// spool. False, with out emptied, when nothing is loaded.
bool copyBbsView(CopyBbsView* out);

// The price per kg of the new spool, typed on the card. Without a call the
// template's price per kg goes along unchanged; a typed one goes exactly as
// typed, and 0 is an emptied field and sends no price (copyRulesCostPerKg).
void copyBbsSetPrice(float typed_price);

// Creates the copy. Behind Spoolman: the POST with every field, then the
// profiles per model, the suppliers and the K profiles, each only when the
// template has some. A step after the POST that fails is logged with its HTTP
// code, sets its CopyMissing bit and does not stop the rest: the spool stays
// once it is created. With Bambuddy's own inventory: the fields a copy always
// had, read fresh from the template, and no bits.
// Returns the HTTP code of the create (200 when the spool was made, its id in
// *out_spool_id) or, when the template cannot be read, that read's code and
// nothing is created. out_spool_id and out_missing may be null.
int copyBbsCreate(const CopyBbsCreate& in, int* out_spool_id, uint8_t* out_missing);

// The CopyMissing bits of the last copyBbsCreate(), 0 before the first one.
// Kept across copyBbsClear(), so the status line can still ask afterwards.
uint8_t copyBbsLastMissing();

// Frees what copyBbsLoad() holds and forgets a typed price.
void copyBbsClear();
