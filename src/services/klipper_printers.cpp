#include "services/klipper_printers.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/bambuddy_api.h"
#include "services/klipper_store.h"
#include "services/moonraker_api.h"
#include "services/spool_color.h"
#include "services/text_util.h"

namespace {

// A stored printer and its key, copied for one operation. The key is wiped
// when the copy goes out of scope, on whichever return that is.
struct KlipperLink {
  KlipperPrinterCfg cfg;
  char key[KL_KEY_MAX];
  ~KlipperLink() {
    volatile char* p = key;
    for (size_t i = 0; i < sizeof(key); i++) p[i] = 0;
  }
};

bool linkFor(int printer_id, KlipperLink* out) {
  if (!klipperIsPrinterId(printer_id)) return false;
  if (!klipperStoreFind(static_cast<uint32_t>(printer_id), &out->cfg)) return false;
  klipperStoreKey(out->cfg.id, out->key, sizeof(out->key));   // "" when it has none
  return true;
}

// The facts of the last klipperGetAmsState(), for the names of the bays and
// the note on cut fields. One block of about 1.4 kB from the PSRAM, made on the
// first read and never freed; s_facts_id says whose facts they are (0: none).
KlipperFacts* s_facts = nullptr;
int s_facts_id = 0;

KlipperFacts* factsBlock() {
  if (!s_facts) s_facts = static_cast<KlipperFacts*>(moonrakerAlloc(sizeof(KlipperFacts)));
  return s_facts;
}

// What one assignment needs besides the stack, about 7 kB: the facts before
// and after the commands, and the plan (KlipperPlan is 4 kB, R8).
struct AssignWork {
  KlipperFacts before;
  KlipperFacts after;
  KlipperPlan plan;
};

class WorkBlock {
 public:
  WorkBlock() : block_(static_cast<AssignWork*>(moonrakerAlloc(sizeof(AssignWork)))) {}
  ~WorkBlock() { moonrakerFree(block_); }
  WorkBlock(const WorkBlock&) = delete;
  WorkBlock& operator=(const WorkBlock&) = delete;
  AssignWork* get() const { return block_; }

 private:
  AssignWork* block_;
};

// One field as a bay. A bay with no spool is drawn as an empty one (R26).
void fillTray(const KlipperField& f, uint8_t tray_id, AmsSlotTray* tray) {
  *tray = AmsSlotTray{};
  tray->tray_id = tray_id;
  // The printer reports no grams, no fill level and no nozzle range.
  tray->remain_g = AMS_REMAIN_NA;
  tray->remain = AMS_REMAIN_NA;
  tray->nozzle_min = AMS_REMAIN_NA;
  tray->nozzle_max = AMS_REMAIN_NA;
  tray->spool_id = f.spool_id;
  utf8Cut(f.material, sizeof(tray->name) - 1, tray->name, sizeof(tray->name));
  utf8Cut(f.material, sizeof(tray->type) - 1, tray->type, sizeof(tray->type));
  spoolColorParse(f.color, &tray->color);
  tray->exists = f.spool_id > 0;
  tray->active = f.loaded;
}

}  // namespace

bool klipperAvailable() {
  switch (backendMode()) {
    case BACKEND_SPOOLMAN:
      return backendIsConfigured();
    case BACKEND_BAMBUDDY:
      return bbInventoryMode() == BB_INV_SPOOLMAN && bbSpoolmanUrl()[0] != '\0';
    default:
      return false;
  }
}

const char* klipperScaleSpoolman() {
  return klipperAvailable() ? backendFdbBaseUrl() : "";
}

bool klipperIsPrinterId(int id) {
  return id >= KL_ID_BASE;
}

bool klipperHasPrinters() {
  return klipperAvailable() && klipperStoreCount() > 0;
}

void klipperAppendPrinters(AmsPrinterList& out) {
  const int count = klipperStoreCount();
  for (int i = 0; i < count; i++) {
    KlipperPrinterCfg cfg;
    if (!klipperStoreAt(i, &cfg)) continue;
    if (out.count >= AMS_MAX_PRINTERS) {
      logSDf("KLIPPER: more than %d printers in the list, rest ignored", AMS_MAX_PRINTERS);
      break;
    }
    AmsPrinter& dst = out.p[out.count];
    dst = AmsPrinter{};
    dst.id = static_cast<int>(cfg.id);
    utf8Cut(cfg.name, sizeof(dst.name) - 1, dst.name, sizeof(dst.name));
    dst.active = true;
    // Nothing asks the printer here: only a test that found it gone says so.
    dst.online = cfg.last.test != KT_UNREACHABLE;
    dst.kind = AMS_PRINTER_KLIPPER;
    dst.layout = (cfg.last.layout <= KL_LAYOUT_AFC) ? cfg.last.layout : KL_LAYOUT_NONE;   // R18
    dst.bays = cfg.last.fields;
    out.count++;
  }
}

int klipperGetAmsState(int printer_id, AmsSlotState& out) {
  out = AmsSlotState{};
  KlipperLink link = {};
  if (!linkFor(printer_id, &link)) return KL_E_NO_PRINTER;
  KlipperFacts* f = factsBlock();
  if (!f) return -1;

  s_facts_id = 0;   // what is read now replaces what was there, also when it fails
  const int code = moonrakerFetchFacts(link.cfg, link.key, f);
  if (code != 200) {
    logSDf("KLIPPER: %s not read (%d)", link.cfg.name, code);
    return code;
  }
  s_facts_id = printer_id;

  out.printer_id = printer_id;
  utf8Cut(link.cfg.name, sizeof(out.printer) - 1, out.printer, sizeof(out.printer));
  utf8Cut(klipperStateWord(f->state), sizeof(out.state) - 1, out.state, sizeof(out.state));
  out.job_percent = (f->progress >= 0 && f->progress <= 100) ? f->progress : AMS_JOB_NA;
  out.valid = true;
  out.connected = true;
  out.conn_known = true;
  out.ams_exists = true;

  const uint8_t units = klipperUnitCount(*f);
  for (uint8_t u = 0; u < units && out.unit_count < AMS_MAX_UNITS; u++) {
    KlipperUnit ku;
    if (!klipperUnit(*f, u, &ku)) break;
    AmsSlotUnit& dst = out.unit[out.unit_count];
    dst = AmsSlotUnit{};
    dst.ams_id = u;
    dst.model = AMS_MODEL_KLIPPER;
    dst.humidity = AMS_HUMIDITY_NA;
    dst.temp_c10 = AMS_TEMP_NA;
    dst.dry_minutes = AMS_REMAIN_NA;
    dst.dry_target_c = AMS_REMAIN_NA;
    utf8Cut(ku.label, sizeof(dst.label) - 1, dst.label, sizeof(dst.label));
    for (uint8_t i = 0; i < ku.count && dst.tray_count < AMS_MAX_TRAYS; i++) {
      const unsigned idx = static_cast<unsigned>(ku.first) + i;
      if (idx >= f->field_count || idx >= KL_FIELDS_MAX) break;
      fillTray(f->field[idx], i, &dst.tray[dst.tray_count]);
      dst.tray_count++;
    }
    out.unit_count++;
  }
  logSDf("KLIPPER: %s, layout %d, %d field(s) in %d unit(s), state '%s'", link.cfg.name,
         static_cast<int>(f->layout), static_cast<int>(f->field_count),
         static_cast<int>(out.unit_count), out.state);
  return 200;
}

int klipperAssign(int spool_id, int printer_id, int ams_id, int tray_id) {
  if (spool_id <= 0) return -1;
  KlipperLink link = {};
  if (!linkFor(printer_id, &link)) return KL_E_NO_PRINTER;

  // Root: a spool number is the number in ONE Spoolman. At a printer whose
  // Moonraker talks to another one, number 87 is somebody else's spool 87.
  // Fix: the connection test stored the verdict with the printer; when it said
  // "other Spoolman" no command is sent, not even a harmless-looking one.
  // NOT-TO-DO: asking the printer again here. The test is the place that
  // compares the two Spoolmans (address, then newest spool), and it costs a
  // request to each; the page makes the user run it again after a change.
  if (link.cfg.last.test == KT_OTHER_SPOOLMAN) {
    logSDf("KLIPPER: %s talks to another Spoolman, spool %d not sent", link.cfg.name, spool_id);
    return KL_E_OTHER_SPOOLMAN;
  }

  const WorkBlock work;
  AssignWork* w = work.get();
  if (!w) {
    logSD("KLIPPER: no memory to assign a spool");
    return -1;
  }

  // The plan is made from the printer as it is now: a gate may have been added
  // or a spool moved since the view drew it.
  int code = moonrakerFetchFacts(link.cfg, link.key, &w->before);
  if (code != 200) return code;
  if (ams_id < 0 || ams_id > 255 || tray_id < 0 || tray_id > 255) return KL_E_NO_FIELD;
  const int field = klipperBayField(w->before, static_cast<uint8_t>(ams_id),
                                    static_cast<uint8_t>(tray_id));
  if (field < 0 || !klipperPlanAssign(w->before, static_cast<uint8_t>(field), spool_id, &w->plan)) {
    logSDf("KLIPPER: %s has no bay %d/%d for spool %d", link.cfg.name, ams_id, tray_id, spool_id);
    return KL_E_NO_FIELD;
  }

  // The spool leaves the other fields first (it sits in one place), then goes
  // into this one; for a tool that is the active one the printer's active
  // spool follows. An empty part is not sent.
  if (w->plan.clear[0]) {
    code = moonrakerRunScript(link.cfg, link.key, w->plan.clear);
    if (code != 200) return code;
  }
  if (w->plan.set[0]) {
    code = moonrakerRunScript(link.cfg, link.key, w->plan.set);
    if (code != 200) return code;
  }
  if (w->plan.set_active) {
    code = moonrakerSetActiveSpool(link.cfg, link.key, w->plan.active_spool);
    if (code != 200) return code;
  }

  // Root: Moonraker answers "ok" for a command of a macro that does not exist
  // or a lane that is misspelled, and Happy Hare in "pull" mode only compares
  // with Spoolman after the command. The answers above prove nothing.
  // Fix: the printer is read again and klipperFieldHolds() decides. A field
  // that does not hold the spool yet gets one more look after a second; only
  // a yes is "assigned", anything else is KL_E_NOT_HELD.
  // NOT-TO-DO: reporting the answer of the commands as the result, or waiting
  // before the first read: most printers have it done by then, and the
  // second is only for the slow ones. The pause stands on the loop task like
  // the requests do (HttpStallTime of the dispatcher), which is accepted.
  // See test_klipper_wiring.py, section 5.
  code = moonrakerFetchFacts(link.cfg, link.key, &w->after);
  bool held = code == 200 && klipperFieldHolds(w->after, static_cast<uint8_t>(field), spool_id);
  if (!held) {
    delay(1000);
    code = moonrakerFetchFacts(link.cfg, link.key, &w->after);
    held = code == 200 && klipperFieldHolds(w->after, static_cast<uint8_t>(field), spool_id);
  }
  if (!held) {
    logSDf("KLIPPER: %s does not hold spool %d in bay %d/%d (read %d)", link.cfg.name,
           spool_id, ams_id, tray_id, code);
    return code == 200 ? KL_E_NOT_HELD : code;
  }
  logSDf("KLIPPER: spool %d is in %s, bay %d/%d", spool_id, link.cfg.name, ams_id, tray_id);

  // The spool's place in Spoolman is the printer's name, unless Happy Hare
  // writes it itself. The answer only goes to the log: the spool is in the
  // printer either way. The location the scale shows for the parked spool is
  // kept in step, as ams_pick.cpp setAmsLocation() does it.
  if (klipperWritesLocation(w->after, klipperLocSwitch())) {
    const int where = backendPatchSpoolLocation(cfg_spoolman_base, spool_id, link.cfg.name, 8000);
    logSDf("KLIPPER: location of spool %d -> '%s', HTTP %d", spool_id, link.cfg.name, where);
    if (backendWriteOk(where) && sm_found && sm_id == spool_id) {
      snprintf(sm_location_name, sizeof(sm_location_name), "%s", link.cfg.name);
      sm_location_id = 0;
    }
  }
  return 200;
}

bool klipperFieldName(int printer_id, int ams_id, int tray_id, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!s_facts || s_facts_id != printer_id) return false;
  if (ams_id < 0 || ams_id > 255 || tray_id < 0 || tray_id > 255) return false;
  const int idx = klipperBayField(*s_facts, static_cast<uint8_t>(ams_id),
                                  static_cast<uint8_t>(tray_id));
  if (idx < 0) return false;
  const KlipperField& field = s_facts->field[idx];
  switch (s_facts->layout) {
    case KL_LAYOUT_MMU:
      snprintf(out, n, "MMU %d", static_cast<int>(field.number));
      return true;
    case KL_LAYOUT_AFC:
      utf8Cut(field.label, n - 1, out, n);
      return out[0] != '\0';
    case KL_LAYOUT_TOOLS:
      snprintf(out, n, "T%d", static_cast<int>(field.number));
      return true;
    default:
      return false;   // the one active spool has no name of its own
  }
}

const char* klipperPrinterName(int printer_id) {
  static char s_name[KL_NAME_MAX];
  KlipperPrinterCfg cfg;
  if (klipperIsPrinterId(printer_id) && klipperStoreFind(static_cast<uint32_t>(printer_id), &cfg)) {
    utf8Cut(cfg.name, sizeof(s_name) - 1, s_name, sizeof(s_name));
  } else {
    s_name[0] = '\0';
  }
  return s_name;
}

bool klipperTruncated(int printer_id) {
  return s_facts && s_facts_id == printer_id && s_facts->truncated;
}
