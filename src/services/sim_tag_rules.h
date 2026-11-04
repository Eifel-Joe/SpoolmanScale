#pragma once

#include <stdint.h>
#include <string.h>

// ============================================================
//  A MADE-UP TAG ON THE READER (acceptance build, Spec 5.3 AA, v0.8.7-beta.2)
//
//  The lookup of a tag nobody knows - the card "Nicht in BamBuddy", the
//  recheck's pace (the original's o11), a failed archive list (o09) - runs
//  only once a tag lies on the reader. The acceptance cannot place one, so the
//  reader's door (hardware/nfc.cpp) answers with this: an NTAG215 with a 7
//  byte UID, factory fresh, an empty NDEF message on it. It is never written.
//  No switch here, the host test compiles it (test_acceptance_sim_rules.py).
// ============================================================

constexpr uint8_t SIM_TAG_UID_LEN   = 7;
constexpr uint8_t SIM_TAG_LAST_PAGE = 134;   // NTAG215: pages 0..134

inline int simTagHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Fourteen hex digits, nothing else.
inline bool simTagUidOk(const char* text) {
  if (!text || strlen(text) != 2u * SIM_TAG_UID_LEN) return false;
  for (const char* p = text; *p; p++) if (simTagHex(*p) < 0) return false;
  return true;
}

inline bool simTagUidParse(const char* text, uint8_t uid[SIM_TAG_UID_LEN]) {
  if (!simTagUidOk(text)) return false;
  for (uint8_t i = 0; i < SIM_TAG_UID_LEN; i++) {
    uid[i] = (uint8_t)((simTagHex(text[2 * i]) << 4) | simTagHex(text[2 * i + 1]));
  }
  return true;
}

// Page `page` of the empty tag as the reader would give it: the UID with its
// two check bytes (ISO 14443-3: BCC0 = 0x88 ^ UID0 ^ UID1 ^ UID2, BCC1 = UID3
// ^ .. ^ UID6), the capability container of an NTAG215 (E1 10 3E 00: NDEF,
// version 1.0, 496 bytes, writable), an empty NDEF message and its end mark;
// zeros after that. False past the last page.
inline bool simNtagPage(const uint8_t uid[SIM_TAG_UID_LEN], uint8_t page, uint8_t out[4]) {
  if (page > SIM_TAG_LAST_PAGE) return false;
  memset(out, 0, 4);
  switch (page) {
    case 0:
      out[0] = uid[0]; out[1] = uid[1]; out[2] = uid[2];
      out[3] = (uint8_t)(0x88 ^ uid[0] ^ uid[1] ^ uid[2]);
      break;
    case 1:
      out[0] = uid[3]; out[1] = uid[4]; out[2] = uid[5]; out[3] = uid[6];
      break;
    case 2:
      out[0] = (uint8_t)(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]);
      break;
    case 3:
      out[0] = 0xE1; out[1] = 0x10; out[2] = 0x3E; out[3] = 0x00;
      break;
    case 4:
      out[0] = 0x03; out[1] = 0x00; out[2] = 0xFE; out[3] = 0x00;
      break;
    default:
      break;
  }
  return true;
}
