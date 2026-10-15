#include "tag_family.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bambu/bambu_catalog.h"
#include "hardware/flash_log.h"
#include "hardware/sd_logger.h"
#include "services/user_options.h"

// ---- where and how ------------------------------------------
//
// The area starts right behind the Bambu catalog, which starts right behind
// the log ring. It is split into two halves that take turns. The active half
// starts with a header slot, then one 8 byte record per change:
//
//   header   'T' 'F' version FF  generation (4 bytes, little endian)
//   record   uid (4)  family  FF FF  crc8 of the first 7 bytes
//
// A slot still erased (all FF) is free, and the first one ends the half. A
// record whose crc does not match - half written when the power went - is
// skipped. Family 0 in a record is a tombstone: the UID was forgotten.
// When the active half is full, the live entries go into the other half,
// which is erased first and gets its header last: until that header is
// written, the old half is still the newest valid one. At start the header
// with the higher generation wins.
//
// A record costs one flash write of 8 bytes (a 128 byte log record measured
// 0.43 ms, 20.09.2026). Erasing a half is two 4 kB sectors at 54 ms each,
// once per compaction.
#define TAG_FAMILY_OFFSET  (FLASH_LOG_BYTES + BAMBU_CATALOG_BYTES)

static constexpr size_t  HALF_BYTES = TAG_FAMILY_BYTES / 2;
// The flash erases in sectors; a half has to be whole sectors.
static constexpr size_t  FLASH_SECTOR_BYTES = 4096;
static_assert(HALF_BYTES % FLASH_SECTOR_BYTES == 0, "a half must be whole flash sectors");
static_assert(TAG_FAMILY_OFFSET % FLASH_SECTOR_BYTES == 0, "the area must start on a sector");
static constexpr size_t  SLOT_BYTES = 8;
static constexpr size_t  HALF_SLOTS = HALF_BYTES / SLOT_BYTES;   // slot 0 is the header
// How many UIDs are kept: three quarters of a half, so a compaction leaves a
// quarter free for new records instead of running again on the next one.
static constexpr int     CAPACITY = (int)(HALF_SLOTS - 1) * 3 / 4;
static constexpr uint8_t HEADER_MAGIC0  = 'T';
static constexpr uint8_t HEADER_MAGIC1  = 'F';
static constexpr uint8_t LAYOUT_VERSION = 1;
static constexpr uint8_t ERASED         = 0xFF;
static constexpr size_t  READ_CHUNK_SLOTS = 32;
static constexpr uint8_t CRC8_POLY      = 0x07;

struct Entry {
  uint8_t uid[4];
  char    family;
};

static Entry*   s_tab = nullptr;     // oldest first; PSRAM when there is some
static int      s_count = 0;
static bool     s_loaded = false;
static const esp_partition_t* s_part = nullptr;   // nullptr: RAM only
static int      s_half = 0;
static uint32_t s_gen = 0;
static size_t   s_next = 0;          // next free slot of the active half

static bool persisted(char f) {
  return f == TAG_FAMILY_CREALITY || f == TAG_FAMILY_BAMBU || f == TAG_FAMILY_SNAPMAKER;
}

static const char* familyName(char f) {
  switch (f) {
    case TAG_FAMILY_CREALITY:     return "Creality";
    case TAG_FAMILY_BAMBU:        return "Bambu";
    case TAG_FAMILY_SNAPMAKER:    return "Snapmaker";
    case TAG_FAMILY_NOT_CREALITY: return "not Creality (this session)";
    case TAG_FAMILY_BLANK:        return "blank (this session)";
    default:                      return "unknown";
  }
}

static uint8_t crc8(const uint8_t* p, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ CRC8_POLY) : (uint8_t)(c << 1);
  }
  return c;
}

// ---- the table in RAM ---------------------------------------

static int tableFind(const uint8_t uid[4]) {
  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_tab[i].uid, uid, 4) == 0) return i;
  }
  return -1;
}

static void tableRemove(int i) {
  memmove(&s_tab[i], &s_tab[i + 1], (s_count - i - 1) * sizeof(Entry));
  s_count--;
}

// The newest goes to the end; when the table is full the oldest falls off.
static void tableSet(const uint8_t uid[4], char family) {
  const int i = tableFind(uid);
  if (i >= 0) tableRemove(i);
  if (family == TAG_FAMILY_UNKNOWN) return;
  if (s_count == CAPACITY) tableRemove(0);
  memcpy(s_tab[s_count].uid, uid, 4);
  s_tab[s_count].family = family;
  s_count++;
}

// ---- the area in flash --------------------------------------

static size_t slotOffset(int half, size_t slot) {
  return TAG_FAMILY_OFFSET + (size_t)half * HALF_BYTES + slot * SLOT_BYTES;
}

static bool slotErased(const uint8_t* s) {
  for (size_t i = 0; i < SLOT_BYTES; i++) if (s[i] != ERASED) return false;
  return true;
}

static void encodeRecord(const uint8_t uid[4], char family, uint8_t out[SLOT_BYTES]) {
  memcpy(out, uid, 4);
  out[4] = (uint8_t)family;
  out[5] = ERASED;
  out[6] = ERASED;
  out[7] = crc8(out, SLOT_BYTES - 1);
}

// A write failed: from here on the memory lives in RAM only, which costs a
// tag nothing but its one wrong first key after the next restart.
static void flashGivesUp(const char* what) {
  logSDf("NFC: tag memory could not %s - kept in RAM only until restart", what);
  s_part = nullptr;
}

static bool readHeader(int half, uint32_t* gen) {
  uint8_t h[SLOT_BYTES];
  if (esp_partition_read(s_part, slotOffset(half, 0), h, SLOT_BYTES) != ESP_OK) return false;
  if (h[0] != HEADER_MAGIC0 || h[1] != HEADER_MAGIC1 || h[2] != LAYOUT_VERSION) return false;
  memcpy(gen, h + 4, sizeof(*gen));   // little endian on the chip and the host alike
  return *gen != 0xFFFFFFFFUL;
}

// Reads the active half into the table and leaves s_next on its first free
// slot. False when the flash could not be read.
static bool scanHalf() {
  uint8_t buf[READ_CHUNK_SLOTS * SLOT_BYTES];
  for (size_t first = 1; first < HALF_SLOTS; first += READ_CHUNK_SLOTS) {
    const size_t n = (HALF_SLOTS - first < READ_CHUNK_SLOTS) ? HALF_SLOTS - first : READ_CHUNK_SLOTS;
    if (esp_partition_read(s_part, slotOffset(s_half, first), buf, n * SLOT_BYTES) != ESP_OK) {
      return false;
    }
    for (size_t k = 0; k < n; k++) {
      const uint8_t* r = buf + k * SLOT_BYTES;
      if (slotErased(r)) { s_next = first + k; return true; }
      const char family = (char)r[4];
      const bool known = persisted(family) || family == TAG_FAMILY_UNKNOWN;
      if (known && crc8(r, SLOT_BYTES - 1) == r[7]) tableSet(r, family);
    }
  }
  s_next = HALF_SLOTS;
  return true;
}

// Erases a half, writes what the table holds worth keeping, and the header
// with `gen` last. That half is the active one from then on.
static bool writeHalf(int half, uint32_t gen) {
  if (esp_partition_erase_range(s_part, slotOffset(half, 0), HALF_BYTES) != ESP_OK) return false;
  size_t slot = 1;
  for (int i = 0; i < s_count; i++) {
    if (!persisted(s_tab[i].family)) continue;
    uint8_t r[SLOT_BYTES];
    encodeRecord(s_tab[i].uid, s_tab[i].family, r);
    if (esp_partition_write(s_part, slotOffset(half, slot), r, SLOT_BYTES) != ESP_OK) return false;
    slot++;
  }
  uint8_t h[SLOT_BYTES] = {HEADER_MAGIC0, HEADER_MAGIC1, LAYOUT_VERSION, ERASED};
  memcpy(h + 4, &gen, sizeof(gen));
  if (esp_partition_write(s_part, slotOffset(half, 0), h, SLOT_BYTES) != ESP_OK) return false;
  s_half = half;
  s_gen  = gen;
  s_next = slot;
  return true;
}

// One record for a change the table already holds.
static void appendRecord(const uint8_t uid[4], char family) {
  if (!s_part) return;
  if (s_next >= HALF_SLOTS) {
    const unsigned long t0 = millis();
    if (!writeHalf(1 - s_half, s_gen + 1)) { flashGivesUp("be compacted"); return; }
    logSDf("NFC: tag memory compacted into half %d, %u tags kept (%lu ms)",
           s_half, (unsigned)(s_next - 1), millis() - t0);
    return;   // the copy already carries this change
  }
  uint8_t r[SLOT_BYTES];
  encodeRecord(uid, family, r);
  if (esp_partition_write(s_part, slotOffset(s_half, s_next), r, SLOT_BYTES) != ESP_OK) {
    flashGivesUp("be written");
    return;
  }
  s_next++;
}

// Finds the area and reads the newer half into the table.
static void loadFromFlash() {
  const unsigned long t0 = millis();
  s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if (s_part && s_part->size < TAG_FAMILY_OFFSET + TAG_FAMILY_BYTES) s_part = nullptr;
  if (!s_part) {
    logSD("NFC: no data partition for the tag memory - kept in RAM only");
    return;
  }
  uint32_t g0 = 0, g1 = 0;
  const bool v0 = readHeader(0, &g0);
  const bool v1 = readHeader(1, &g1);
  if (!v0 && !v1) {
    if (!writeHalf(0, 1)) { flashGivesUp("be set up"); return; }
    logSDf("NFC: tag memory set up empty (%lu ms)", millis() - t0);
    return;
  }
  s_half = (v1 && (!v0 || g1 > g0)) ? 1 : 0;
  s_gen  = s_half ? g1 : g0;
  if (!scanHalf()) { flashGivesUp("be read"); return; }
  logSDf("NFC: tag memory loaded, %d tags from half %d (generation %lu, %u of %u slots, %lu ms)",
         s_count, s_half, (unsigned long)s_gen, (unsigned)s_next, (unsigned)HALF_SLOTS,
         millis() - t0);
}

static void load() {
  if (s_loaded) return;
  s_loaded = true;
  s_tab = (Entry*)heap_caps_malloc(CAPACITY * sizeof(Entry), MALLOC_CAP_SPIRAM);
  if (!s_tab) s_tab = (Entry*)malloc(CAPACITY * sizeof(Entry));
  if (!s_tab) {
    logSD("NFC: no memory for the tag memory - every tag counts as unknown");
    return;
  }
  loadFromFlash();
}

TagFamily tagFamilyGet(const uint8_t uid[4]) {
  if (!g_creality_tags) return TAG_FAMILY_UNKNOWN;
  load();
  const int i = s_tab ? tableFind(uid) : -1;
  return i < 0 ? TAG_FAMILY_UNKNOWN : (TagFamily)s_tab[i].family;
}

void tagFamilySet(const uint8_t uid[4], TagFamily family) {
  if (!g_creality_tags) return;
  load();
  if (!s_tab) return;
  const int i = tableFind(uid);
  const char old = i >= 0 ? s_tab[i].family : (char)TAG_FAMILY_UNKNOWN;
  if (old == family) return;   // nothing changes, nothing is written
  tableSet(uid, family);
  logSDf("NFC: %02X:%02X:%02X:%02X remembered as %s", uid[0], uid[1], uid[2], uid[3],
         familyName(family));
  if (persisted(family))   appendRecord(uid, family);
  else if (persisted(old)) appendRecord(uid, TAG_FAMILY_UNKNOWN);   // a tombstone
}
