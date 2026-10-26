#include "ui/sort_entry_internal.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/filament_db.h"
#include "services/sort_entry_source.h"
#include "services/text_util.h"
#include "ui/link_wait_card.h"
#include "ui/ui_common.h"
// Last: lang.h defines T(), which a template parameter of a header above
// would trip over.
#include "lang.h"

// ============================================================
//  THE MATERIAL AND THE LINE STEP, AND WHAT IS TYPED THERE
//
//  The material step offers the brand's materials (the inventory's in its
//  spelling, then the maker's of the database), the material chosen last
//  first when the brand is the one chosen last, then under "Other
//  materials" the rest. A material chosen queues the database loads of the
//  maker for it; sortEntryTick() hands sortSourceTick()'s answer to
//  sortMaterialLoads(), which puts up the waiting card while they run and
//  shows the line step once they are in. The line step offers "no line",
//  then the lines of the inventory, the database and the usual ones; a line
//  chosen builds the colour and the weight lists (no request) and goes on
//  to the colour step.
//
//  What is typed on the keyboard is held against the list of its step with
//  sortEntrySame() (plan Festlegung 5): a match is that entry, in the
//  spelling of the list, and the next step says "Already there"; anything
//  else goes on as typed.
// ============================================================

// The waiting card stands for the material's loads.
static bool s_card = false;

// ------------------------------------------------------------
//  "Already there"
// ------------------------------------------------------------

// The name a typed one matched, for the top of the next step; null for none.
static void setExists(const char* name) {
  SortUi& ui = sortUi();
  if (name) utf8Cut(name, sizeof(ui.exists) - 1, ui.exists, sizeof(ui.exists));
  else      ui.exists[0] = '\0';
}

void sortExistsCaption(lv_obj_t* body) {
  SortUi& ui = sortUi();
  if (!ui.exists[0] || !body) return;
  char text[SORT_NAME_MAX + 48];
  snprintf(text, sizeof(text), T(STR_SORT_EXISTS), ui.exists);
  ui.exists[0] = '\0';
  sortCaption(body, text);
}

// ------------------------------------------------------------
//  Step 2: the material
// ------------------------------------------------------------

// What the entry holds after the material, the line or the colour: gone
// when an earlier step is chosen anew.
static void clearColour(SortEntry& e) {
  e.colour[0] = '\0';
  e.hex[0] = '\0';
  e.clear = false;
  e.filament_id = 0;
}

// The material chosen last, when the brand is the one chosen last: its
// entry in the list, added at the end when the list does not have it (typed
// last time, and not in the inventory since). -1 for none.
static int lastMaterial(SortData* d) {
  const SortUi& ui = sortUi();
  if (!d || !ui.last_material[0] || !sortEntrySame(ui.entry.vendor, ui.last_vendor)) return -1;
  const int k = sortChoicesFind(d->materials, ui.last_material);
  return k >= 0 ? k : sortChoicesAdd(&d->materials, ui.last_material, nullptr, SF_LAST);
}

// Not the brand's: a material of the database the maker has no filament
// of, or a usual one where there is no database.
static bool otherMaterial(const SortChoice& c) {
  return c.from == SF_DB_OTHER || c.from == SF_USUAL;
}

void sortShowMaterials() {
  SortUi& ui = sortUi();
  SortData* d = sortData();
  ui.step = SS_MATERIAL;
  sortTitle(ui.entry.vendor);
  lv_obj_t* body = sortNewBody();
  logLvMem("sortentry-materials/pre", 0);
  sortExistsCaption(body);
  sortCaption(body, T(STR_MAT_TITLE));
  int tiles = 0;
  const int last = lastMaterial(d);
  if (last >= 0 && lvPoolHasRoomForRow()) {
    sortTile(body, d->materials.item[last].name, SN_MATERIAL, last);
    tiles++;
  }
  bool others = false;
  for (int i = 0; d && i < d->materials.n; i++) {
    if (i == last) continue;
    if (!lvPoolHasRoomForRow()) { logSDf("SortEntry: LVGL pool low, materials cut at %d", i); break; }
    // The rest under a caption of its own, once the brand's are on screen.
    if (!others && tiles > 0 && otherMaterial(d->materials.item[i])) {
      sortCaption(body, T(STR_SORT_OTHER_MATERIALS));
      others = true;
    }
    sortTile(body, d->materials.item[i].name, SN_MATERIAL, i);
    tiles++;
  }
  sortNewTile(body);
  logLvMem("sortentry-materials/post", tiles);
}

// A material chosen or typed: the database loads for it are queued, and
// sortMaterialLoads() waits for them on the next sortSourceTick() answer.
static void chooseMaterial(const char* name) {
  SortUi& ui = sortUi();
  if (!name || !name[0] || !sortData()) return;
  // name may point into the list: copied first.
  char chosen[sizeof(ui.entry.material)];
  utf8Cut(name, sizeof(chosen) - 1, chosen, sizeof(chosen));
  memcpy(ui.entry.material, chosen, sizeof(ui.entry.material));
  ui.entry.line[0] = '\0';
  clearColour(ui.entry);
  logSDf("SortEntry: material %s", ui.entry.material);
  sortSourceMaterial(ui.entry.vendor, ui.entry.material);
  ui.waiting = true;
}

void sortMaterialLoads(SortLoad load) {
  SortUi& ui = sortUi();
  if (!ui.waiting) {
    s_card = false;
    return;
  }
  if (ui.step != SS_MATERIAL || !sortData()) {
    // Closed or left meanwhile: nothing to go on to.
    ui.waiting = false;
    if (s_card) linkWaitCardHide();
    s_card = false;
    return;
  }
  if (load != SL_RUNNING) {
    // In, or given up: the lines from what there is. The card goes after
    // the step is built, so the list under it does not show half done.
    ui.waiting = false;
    sortSourceLines(ui.entry.material);
    sortShowLines();
    if (s_card) linkWaitCardHide();
    s_card = false;
    return;
  }
  if (!s_card) {
    // Not sortWaitShow(): the loop goes on while the loads run, so nothing
    // has to be drawn ahead of time, and right after sortSourceTick() a
    // forced refresh would only widen the window for dbPickTick().
    linkWaitCardShow();
    linkWaitCardTitle(T(STR_DBPICK_LOADING));
    s_card = true;
  }
  linkWaitCardBytes(fdbBytes());
  if (linkWaitCardCancelTake()) {
    // The loads run out on their own; the next material drops them.
    logSDf("SortEntry: database loads for %s cancelled", ui.entry.material);
    ui.waiting = false;
    ui.exists[0] = '\0';
    linkWaitCardHide();
    s_card = false;
  }
}

void sortMaterialTyped(const char* text) {
  SortData* d = sortData();
  const int k = d ? sortChoicesFind(d->materials, text) : -1;
  const char* name = k >= 0 ? d->materials.item[k].name : text;
  if (k >= 0) logSDf("SortEntry: typed material is %s", name);
  setExists(k >= 0 ? name : nullptr);
  chooseMaterial(name);
}

// ------------------------------------------------------------
//  Step 3: the line
// ------------------------------------------------------------

void sortShowLines() {
  SortUi& ui = sortUi();
  SortData* d = sortData();
  ui.step = SS_LINE;
  char title[80];
  snprintf(title, sizeof(title), T(STR_SORT_LINE_TITLE), ui.entry.material);
  sortTitle(title);
  lv_obj_t* body = sortNewBody();
  logLvMem("sortentry-lines/pre", 0);
  sortExistsCaption(body);
  int tiles = 0;
  if (lvPoolHasRoomForRow()) {
    sortTile(body, T(STR_SORT_NO_LINE), SN_NO_LINE, 0);
    tiles++;
  }
  for (int i = 0; d && i < d->lines.n; i++) {
    if (!lvPoolHasRoomForRow()) { logSDf("SortEntry: LVGL pool low, lines cut at %d", i); break; }
    sortTile(body, d->lines.item[i].name, SN_LINE, i);
    tiles++;
  }
  sortNewTile(body);
  logLvMem("sortentry-lines/post", tiles);
}

// A line chosen or typed, "" for none: the colour and the weight lists of
// this material and line, then the colour step.
static void chooseLine(const char* line) {
  SortUi& ui = sortUi();
  if (!line || !sortData()) return;
  char chosen[sizeof(ui.entry.line)];
  utf8Cut(line, sizeof(chosen) - 1, chosen, sizeof(chosen));
  memcpy(ui.entry.line, chosen, sizeof(ui.entry.line));
  clearColour(ui.entry);
  logSDf("SortEntry: line %s", ui.entry.line[0] ? ui.entry.line : "(none)");
  sortSourceColours(ui.entry.material, ui.entry.line);
  sortSourceWeights(ui.entry.material);
  sortShowColours();
}

void sortLineTyped(const char* text) {
  SortData* d = sortData();
  const int k = d ? sortChoicesFind(d->lines, text) : -1;
  const char* name = k >= 0 ? d->lines.item[k].name : text;
  if (k >= 0) logSDf("SortEntry: typed line is %s", name);
  setExists(k >= 0 ? name : nullptr);
  chooseLine(name);
}

// ------------------------------------------------------------
//  The maker typed (the "new" tile of the maker step)
// ------------------------------------------------------------

void sortMakerTyped(const char* text) {
  const SortData* d = sortData();
  // The inventory first: its spelling is what finds the filaments again
  // (plan Festlegung 8). Then the makers of the index.
  const int k = d ? sortChoicesFind(d->brands, text) : -1;
  const char* hit = k >= 0 ? d->brands.item[k].name : nullptr;
  for (int i = 0; !hit && fdbIndexReady() && i < fdbMakerCount(); i++) {
    const FdbMaker* m = fdbMaker(i);
    if (m && sortEntrySame(m->name, text)) hit = m->name;
  }
  if (hit) logSDf("SortEntry: typed maker is %s", hit);
  setExists(hit);
  sortChooseMaker(hit ? hit : text);
}

// ------------------------------------------------------------
//  Taps
// ------------------------------------------------------------

bool sortStepsNav(uint8_t nav, int arg) {
  SortUi& ui = sortUi();
  SortData* d = sortData();
  switch (nav) {
    case SN_MATERIAL:
      if (ui.step == SS_MATERIAL && !ui.waiting && d && arg < d->materials.n) {
        setExists(nullptr);
        chooseMaterial(d->materials.item[arg].name);
      }
      return true;
    case SN_LINE:
      if (ui.step == SS_LINE && d && arg < d->lines.n) {
        setExists(nullptr);
        chooseLine(d->lines.item[arg].name);
      }
      return true;
    case SN_NO_LINE:
      if (ui.step == SS_LINE) {
        setExists(nullptr);
        chooseLine("");
      }
      return true;
    default:
      return false;
  }
}
