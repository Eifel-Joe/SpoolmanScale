#include "services/spool_link_store.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "app/backend_switch.h"
#include "hardware/sd_logger.h"
#include "services/spool_link_slots.h"

// The slots, in PSRAM from the first link on; never freed, 1.1 kB there.
static SpoolLinkSlots s_links = { nullptr, SPOOL_LINK_SLOTS, 0 };
static bool s_no_psram = false;
// Puts come from a worker task (the label's dates) as well as from the loop.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// The slots, made on first use; nullptr without PSRAM (said once in the log).
static bool made() {
  if (s_links.slot || s_no_psram) return s_links.slot != nullptr;
  void* p = heap_caps_calloc(SPOOL_LINK_SLOTS, sizeof(SpoolLinkSlot), MALLOC_CAP_SPIRAM);
  if (!p) {
    s_no_psram = true;
    logSD("Label: no PSRAM for spool links, codes are built from the id");
    return false;
  }
  s_links.slot = static_cast<SpoolLinkSlot*>(p);
  return true;
}

void spoolLinkPut(int id, const char* url, uint32_t gen) {
  if (id <= 0) return;
  // No slots made for a spool without a link: most servers name none.
  if (!s_links.slot && !(url && url[0])) return;
  if (!made()) return;
  // The label says it when it uses the link (labelRenderSpool()); a read
  // happens on every scan, too often for a line of its own.
  portENTER_CRITICAL(&s_mux);
  spoolLinkSlotPut(s_links, id, gen, url);
  portEXIT_CRITICAL(&s_mux);
}

bool spoolLinkCopy(int id, char* out, size_t out_size) {
  if (!out || !out_size) return false;
  out[0] = '\0';
  if (!s_links.slot) return false;
  const uint32_t gen = backendGeneration();
  bool found = false;
  portENTER_CRITICAL(&s_mux);
  const int i = spoolLinkSlotFind(s_links, id, gen);
  // Whole or not at all: a link cut short opens another page.
  if (i >= 0 && strlen(s_links.slot[i].url) < out_size) {
    memcpy(out, s_links.slot[i].url, strlen(s_links.slot[i].url) + 1);
    found = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return found;
}

bool spoolLinkHas(int id) {
  if (!s_links.slot) return false;
  const uint32_t gen = backendGeneration();
  portENTER_CRITICAL(&s_mux);
  const bool has = spoolLinkSlotFind(s_links, id, gen) >= 0;
  portEXIT_CRITICAL(&s_mux);
  return has;
}
