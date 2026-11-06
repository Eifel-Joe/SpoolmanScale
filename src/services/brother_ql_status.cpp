#include "services/brother_ql_status.h"

#include <string.h>

// Brother's status OID 1.3.6.1.4.1.2435.3.3.9.1.6.1.0 in BER: 1.3 as 0x2B,
// 2435 in two bytes of seven bits (0x93 0x03), the rest one byte each.
static const uint8_t STATUS_OID[] = {0x2B, 0x06, 0x01, 0x04, 0x01, 0x93, 0x03,
                                     0x03, 0x03, 0x09, 0x01, 0x06, 0x01, 0x00};
static const char COMMUNITY[] = "public";
#define COMMUNITY_LEN (sizeof(COMMUNITY) - 1)

// BER tags of the few types an SNMP v1 GET uses.
#define BER_INTEGER   0x02
#define BER_OCTETS    0x04
#define BER_NULL      0x05
#define BER_OID       0x06
#define BER_SEQUENCE  0x30
#define PDU_GET       0xA0
#define PDU_RESPONSE  0xA2

// The request number as a BER INTEGER is signed: 0..127 in one byte,
// 128..32767 in two, above that a leading 00 and two.
static size_t idBytes(uint16_t id) { return id < 0x80 ? 1 : id < 0x8000 ? 2 : 3; }

size_t brotherQlStatusRequest(uint16_t request_id, uint8_t* out, size_t cap) {
  const size_t id_n = idBytes(request_id);
  // The content length of each sequence, inside out; every one is below 128
  // and takes one length byte (short form).
  const size_t varbind = (2 + sizeof(STATUS_OID)) + 2;                // OID, NULL
  const size_t list    = 2 + varbind;                                 // one variable
  const size_t pdu     = (2 + id_n) + 3 + 3 + (2 + list);             // id, error, index, list
  const size_t message = 3 + (2 + COMMUNITY_LEN) + (2 + pdu);         // version, community, PDU
  const size_t total   = 2 + message;
  if (!out || cap < total) return 0;

  size_t i = 0;
  out[i++] = BER_SEQUENCE; out[i++] = (uint8_t)message;
  out[i++] = BER_INTEGER;  out[i++] = 1; out[i++] = 0;             // version 1 is 0
  out[i++] = BER_OCTETS;   out[i++] = (uint8_t)COMMUNITY_LEN;
  memcpy(out + i, COMMUNITY, COMMUNITY_LEN); i += COMMUNITY_LEN;
  out[i++] = PDU_GET;      out[i++] = (uint8_t)pdu;
  out[i++] = BER_INTEGER;  out[i++] = (uint8_t)id_n;
  if (id_n == 3) out[i++] = 0;
  if (id_n >= 2) out[i++] = (uint8_t)(request_id >> 8);
  out[i++] = (uint8_t)request_id;
  out[i++] = BER_INTEGER;  out[i++] = 1; out[i++] = 0;             // error-status
  out[i++] = BER_INTEGER;  out[i++] = 1; out[i++] = 0;             // error-index
  out[i++] = BER_SEQUENCE; out[i++] = (uint8_t)list;
  out[i++] = BER_SEQUENCE; out[i++] = (uint8_t)varbind;
  out[i++] = BER_OID;      out[i++] = (uint8_t)sizeof(STATUS_OID);
  memcpy(out + i, STATUS_OID, sizeof(STATUS_OID)); i += sizeof(STATUS_OID);
  out[i++] = BER_NULL;     out[i++] = 0;
  return i;
}

// A reader over the answer that never steps past its end.
struct Ber {
  const uint8_t* p;
  size_t n;
  size_t i;
};

// One tag and its length; true when the tag is `tag` and the whole content
// lies inside the answer. Leaves the reader at the content, `end` after it.
// Lengths in the short form and the long form of one or two bytes.
static bool berOpen(Ber& r, uint8_t tag, size_t* len, size_t* end) {
  if (r.i + 2 > r.n || r.p[r.i] != tag) return false;
  size_t l = r.p[r.i + 1];
  r.i += 2;
  if (l & 0x80) {
    const size_t k = l & 0x7F;
    if (k < 1 || k > 2 || r.i + k > r.n) return false;
    l = 0;
    for (size_t j = 0; j < k; j++) l = (l << 8) | r.p[r.i + j];
    r.i += k;
  }
  if (l > r.n - r.i) return false;
  *len = l;
  *end = r.i + l;
  return true;
}

// An INTEGER of 1 to 3 bytes that is not negative.
static bool readUnsigned(Ber& r, uint32_t* v) {
  size_t len, end;
  if (!berOpen(r, BER_INTEGER, &len, &end) || len < 1 || len > 3 || (r.p[r.i] & 0x80)) return false;
  uint32_t x = 0;
  for (size_t j = 0; j < len; j++) x = (x << 8) | r.p[r.i + j];
  r.i = end;
  *v = x;
  return true;
}

static uint8_t mediaOf(uint8_t b) {
  switch (b) {
    case 0x00: return BQS_NONE;
    case 0x0A: case 0x4A: return BQS_CONTINUOUS;
    case 0x0B: case 0x4B: return BQS_DIE_CUT;
    default:   return BQS_OTHER;
  }
}

bool brotherQlStatusParse(const uint8_t* buf, size_t n, uint16_t request_id,
                          BrotherQlStatus* out) {
  if (!buf || !out) return false;
  Ber r{buf, n, 0};
  size_t len, msg_end, pdu_end, list_end, vb_end, end;
  uint32_t v;

  // The message, exactly as long as it says.
  if (!berOpen(r, BER_SEQUENCE, &len, &msg_end) || msg_end != n) return false;
  if (!readUnsigned(r, &v) || v != 0) return false;                  // SNMP v1
  if (!berOpen(r, BER_OCTETS, &len, &end) || len != COMMUNITY_LEN ||
      memcmp(r.p + r.i, COMMUNITY, COMMUNITY_LEN) != 0) return false;
  r.i = end;
  if (!berOpen(r, PDU_RESPONSE, &len, &pdu_end) || pdu_end != msg_end) return false;
  if (!readUnsigned(r, &v) || v != (uint32_t)request_id) return false;       // this request's
  if (!readUnsigned(r, &v) || v != 0) return false;                  // error-status none
  if (!readUnsigned(r, &v)) return false;                            // error-index
  // One variable, our OID, an octet string of 32 bytes, nothing after it.
  if (!berOpen(r, BER_SEQUENCE, &len, &list_end) || list_end != pdu_end) return false;
  if (!berOpen(r, BER_SEQUENCE, &len, &vb_end) || vb_end != list_end) return false;
  if (!berOpen(r, BER_OID, &len, &end) || len != sizeof(STATUS_OID) ||
      memcmp(r.p + r.i, STATUS_OID, sizeof(STATUS_OID)) != 0) return false;
  r.i = end;
  if (!berOpen(r, BER_OCTETS, &len, &end) || len != BROTHER_QL_STATUS_BYTES || end != vb_end)
    return false;

  // The block (R p.23): head mark 80, size 20, "B", series "4". The bytes
  // the table calls fixed further on differ on Joerg's printer (6, 14, 25)
  // and are not looked at.
  const uint8_t* b = r.p + r.i;
  if (b[0] != 0x80 || b[1] != 0x20 || b[2] != 0x42 || b[3] != 0x34) return false;
  out->model     = b[4];
  out->err1      = b[8];
  out->err2      = b[9];
  out->width_mm  = b[10];
  out->media     = mediaOf(b[11]);
  out->length_mm = b[17];
  return true;
}

// Error information 1 (R p.24): bit 0 no media, 1 end of media (die-cut),
// 2 cutter jam, 4 printer in use, 5 printer turned off; 3, 6, 7 not used.
#define E1_NO_MEDIA   0x01
#define E1_MEDIA_END  0x02
#define E1_ERRORS     0x24   // cutter jam, turned off
// Error information 2: bit 0 replace media, 1 expansion buffer full,
// 2 communication error, 4 cover open, 6 media cannot be fed (also at the
// end), 7 system error; 3 and 5 not used.
#define E2_COVER      0x10
#define E2_NO_FEED    0x40
#define E2_ERRORS     0x87   // replace media, buffer, communication, system

const BrotherQlLabel* brotherQlStatusLabel(const BrotherQlStatus& s) {
  if (s.width_mm == 0) return nullptr;
  if (s.media == BQS_CONTINUOUS) {
    const BrotherQlLabel* l = brotherQlLabel(s.width_mm, 0);
    return l && l->media_type == BROTHER_QL_CONTINUOUS ? l : nullptr;
  }
  if (s.media == BQS_DIE_CUT && s.length_mm != 0) {
    const BrotherQlLabel* l = brotherQlLabel(s.width_mm, s.length_mm);
    return l && l->media_type == BROTHER_QL_DIE_CUT ? l : nullptr;
  }
  return nullptr;
}

// Continuous tape is the same tape by its width (its length byte is 0 by the
// table, and whatever it says does not change the tape); a die-cut label by
// width and length.
static bool sameRoll(const BrotherQlStatus& s, const BrotherQlLabel& set) {
  if (set.media_type == BROTHER_QL_CONTINUOUS)
    return s.media == BQS_CONTINUOUS && s.width_mm == set.width_mm;
  return s.media == BQS_DIE_CUT && s.width_mm == set.width_mm && s.length_mm == set.length_mm;
}

BrotherQlRollVerdict brotherQlRollVerdict(const BrotherQlStatus& s, const BrotherQlLabel& set) {
  if (s.err2 & E2_COVER) return BQV_COVER;
  if (s.media == BQS_NONE || s.width_mm == 0 || (s.err1 & E1_NO_MEDIA)) return BQV_NO_ROLL;
  if ((s.err1 & E1_MEDIA_END) || (s.err2 & E2_NO_FEED)) return BQV_ROLL_END;
  const bool error = (s.err1 & E1_ERRORS) || (s.err2 & E2_ERRORS);
  if (s.media == BQS_OTHER) return error ? BQV_ERROR : BQV_UNCHECKED;
  if (!sameRoll(s, set)) return brotherQlStatusLabel(s) ? BQV_OTHER : BQV_UNKNOWN;
  return error ? BQV_ERROR : BQV_OK;
}
