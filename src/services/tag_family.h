#pragma once

#include <stdint.h>

// ============================================================
//  TAG FAMILY MEMORY
//
//  Bambu, Creality CFS and Snapmaker tags are all 4 byte MIFARE Classic
//  cards, and nothing readable without a key tells them apart. Every refused
//  key leaves the card unreachable for a while (seen on hardware, 12.10.2026),
//  so trying the wrong family first costs the right one its read. The scale
//  therefore remembers, per UID, which family opened the card, and goes
//  straight to it the next time.
//
//    TAG_FAMILY_UNKNOWN       never read: Creality first (when enabled)
//    TAG_FAMILY_CREALITY      opened with the Creality key
//    TAG_FAMILY_BAMBU         read as a Bambu tag
//    TAG_FAMILY_SNAPMAKER     read as a Snapmaker tag: no Creality attempt
//    TAG_FAMILY_NOT_CREALITY  refused the Creality key on the last placement:
//                             Bambu first on the next one. RAM only, and used
//                             once: the next placement drops it, so an unknown
//                             tag alternates Creality first / Bambu first until
//                             one of them reads it. A Creality tag that was
//                             merely badly placed gets Creality again.
//    TAG_FAMILY_BLANK         sector 1 opened with the factory key and held
//                             nothing: a blank tag, which a refused key would
//                             only knock off the reader. Probed with the
//                             factory key first on the next placement. RAM
//                             only; writing the tag makes it Creality.
//
//  Only what a key actually opened is kept across a restart: Creality, Bambu
//  and Snapmaker. A tag nothing opened is never remembered as anything, so a
//  Bambu tag that was badly placed once is not given up on for good.
//
//  Kept in the data partition right behind the log ring and the Bambu catalog
//  (TAG_FAMILY_BYTES), copied into PSRAM on first use and read from there.
//  Not in NVS: that holds every setting in 20 kB and must never fill up.
//  A change appends one 8 byte record; tag_family.cpp has the layout.
//
//  Only while Creality tags are switched on: the Creality probe is what needs
//  it, and with the switch off the scale neither reads nor writes this area,
//  exactly as before it existed.
// ============================================================

// Two halves of 8 kB that take turns, see tag_family.cpp. A host test may
// set a smaller size to make the halves fill quickly.
#ifndef TAG_FAMILY_BYTES
#define TAG_FAMILY_BYTES  (16UL * 1024UL)
#endif

enum TagFamily : char {
  TAG_FAMILY_UNKNOWN      = 0,
  TAG_FAMILY_CREALITY     = 'C',
  TAG_FAMILY_BAMBU        = 'B',
  TAG_FAMILY_SNAPMAKER    = 'S',
  TAG_FAMILY_NOT_CREALITY = 'n',
  TAG_FAMILY_BLANK        = '0',
};

TagFamily tagFamilyGet(const uint8_t uid[4]);
// Records a family for the UID. TAG_FAMILY_UNKNOWN removes the entry.
void tagFamilySet(const uint8_t uid[4], TagFamily family);
