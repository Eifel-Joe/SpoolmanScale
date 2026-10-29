"""Compile the production rules for how a Klipper printer is stored (services/klipper_store_rules.h) and check them: the record of one printer as one line of text and back, the name (cleaned, cut at a character, not taken twice), the address and the key as they may be written, the list of keys, how many more printers the settings memory has room for, and the next number that is never used again."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/klipper_store_rules.h"
#include "services/klipper_rules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char* g_what = "";
#define WHAT(s) (g_what = (s))
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s] line %d: %s\n", g_what, __LINE__, #c); abort(); } } while (0)
#define CHECK_INT(a, b) do { const long long x_ = (long long)(a); const long long y_ = (long long)(b); \
  if (x_ != y_) { fprintf(stderr, "FAIL [%s] line %d: %s is %lld, expected %lld\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)
#define CHECK_STR(a, b) do { const char* x_ = (a); const char* y_ = (b); \
  if (strcmp(x_, y_) != 0) { fprintf(stderr, "FAIL [%s] line %d: %s is \"%s\", expected \"%s\"\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)

// The printers are built by these, with the numbers as plain ints, so that no
// brace initialisation narrows and no field is left out.
static KlipperPrinterCfg mkCfg(unsigned id, const char* name, const char* host, int port) {
  KlipperPrinterCfg c{};
  c.id = (uint32_t)id;
  snprintf(c.name, sizeof(c.name), "%s", name);
  snprintf(c.host, sizeof(c.host), "%s", host);
  c.port = (uint16_t)port;
  return c;
}

static void setOutcome(KlipperPrinterCfg* c, int test, int layout, int fields, int flags, unsigned long at) {
  c->last.test = (uint8_t)test;
  c->last.layout = (uint8_t)layout;
  c->last.fields = (uint8_t)fields;
  c->last.flags = (uint8_t)flags;
  c->last.at = (uint32_t)at;
}

static void expectSame(const KlipperPrinterCfg& a, const KlipperPrinterCfg& b) {
  CHECK_INT(a.id, b.id);
  CHECK_STR(a.name, b.name);
  CHECK_STR(a.host, b.host);
  CHECK_INT(a.port, b.port);
  CHECK_INT(a.last.test, b.last.test);
  CHECK_INT(a.last.layout, b.last.layout);
  CHECK_INT(a.last.fields, b.last.fields);
  CHECK_INT(a.last.flags, b.last.flags);
  CHECK_INT(a.last.at, b.last.at);
}

// The result starts as garbage: a decoder that leaves a field unwritten, or a
// text unterminated, shows.
static bool decode(const char* rec, KlipperPrinterCfg* out) {
  memset(out, 0xA5, sizeof(*out));
  return klipperRecordDecode(rec, out);
}

static bool validUtf8(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    int more = (*p < 0x80) ? 0 : ((*p & 0xE0) == 0xC0) ? 1 : ((*p & 0xF0) == 0xE0) ? 2 : ((*p & 0xF8) == 0xF0) ? 3 : -1;
    if (more < 0) return false;
    p++;
    for (int i = 0; i < more; i++, p++) {
      if ((*p & 0xC0) != 0x80) return false;
    }
  }
  return true;
}

// A cleaner is given exactly the room the real caller has (n), with a fence of
// known bytes behind it that must still be there afterwards. The result is
// copied to `result` (KL_HOST_MAX bytes, the largest of the three).
typedef bool (*CleanFn)(const char*, char*, size_t);

static bool cleaned(CleanFn fn, const char* in, size_t n, char* result) {
  char mem[KL_HOST_MAX + 16];
  memset(mem, 0x7E, sizeof(mem));
  const bool ok = fn(in, mem, n);
  for (size_t k = n; k < sizeof(mem); k++) CHECK(mem[k] == 0x7E);
  if (ok) CHECK(memchr(mem, 0, n) != nullptr);
  snprintf(result, KL_HOST_MAX, "%s", ok ? mem : "");
  return ok;
}

static bool nameClean(const char* in, char* result) { return cleaned(klipperNameClean, in, KL_NAME_MAX, result); }
static bool hostClean(const char* in, char* result) { return cleaned(klipperHostClean, in, KL_HOST_MAX, result); }
static bool keyClean(const char* in, char* result) { return cleaned(klipperKeyClean, in, KL_KEY_MAX, result); }

int main() {
  WHAT("numbers that are stored or shared");
  CHECK_INT(KL_PRINTERS_MAX, 64);
  CHECK_INT(KL_ID_BASE, 100000);
  CHECK_INT(KL_NAME_MAX, 24);
  CHECK_INT(KL_HOST_MAX, 64);
  CHECK_INT(KL_KEY_MAX, 48);
  CHECK_INT(KL_RECORD_MAX, 192);
  CHECK_INT(KL_KEYS_MAX, 3200);
  CHECK_INT(KL_NVS_PER_PRINTER, 8);
  CHECK_INT(KL_NVS_RESERVE, 64);
  CHECK_INT(KL_PORT_DEFAULT, 7125);
  // The verdict of the test is stored with the printer, so its numbers are fixed.
  CHECK_INT(KT_NONE, 0);
  CHECK_INT(KT_OK, 1);
  CHECK_INT(KT_UNREACHABLE, 2);
  CHECK_INT(KT_ACCESS, 3);
  CHECK_INT(KT_KLIPPY, 4);
  CHECK_INT(KT_NO_SPOOLMAN, 5);
  CHECK_INT(KT_SM_OFFLINE, 6);
  CHECK_INT(KT_OTHER_SPOOLMAN, 7);
  CHECK_INT(KT_BAD_ANSWER, 8);
  CHECK_INT(KL_FLAG_TOOLS_WITHOUT_VAR, 1);
  CHECK_INT(KL_FLAG_TRUNCATED, 2);

  WHAT("the record, there and back");
  {
    KlipperPrinterCfg c = mkCfg(100001, "Voron 2.4", "192.168.10.50", 7125);
    setOutcome(&c, KT_OK, KL_LAYOUT_MMU, 8, KL_FLAG_TRUNCATED, 1793232000UL);
    char rec[KL_RECORD_MAX];
    memset(rec, 0xA5, sizeof(rec));
    CHECK(klipperRecordEncode(c, rec, sizeof(rec)));
    CHECK_STR(rec, "1\t100001\tVoron 2.4\t192.168.10.50\t7125\t1\t3\t8\t2\t1793232000");
    KlipperPrinterCfg back;
    CHECK(decode(rec, &back));
    expectSame(c, back);

    // No room: not even the end of the text fits.
    const size_t len = strlen(rec);
    char buf[KL_RECORD_MAX];
    CHECK(klipperRecordEncode(c, buf, len + 1));
    CHECK(!klipperRecordEncode(c, buf, len));
    CHECK(!klipperRecordEncode(c, buf, 10));

    // A printer that never was tested.
    KlipperPrinterCfg fresh = mkCfg(100002, "Trident", "trident.local", 80);
    CHECK(klipperRecordEncode(fresh, rec, sizeof(rec)));
    CHECK_STR(rec, "1\t100002\tTrident\ttrident.local\t80\t0\t0\t0\t0\t0");
    CHECK(decode(rec, &back));
    expectSame(fresh, back);
  }

  WHAT("the longest record fits KL_RECORD_MAX");
  {
    const std::string name(KL_NAME_MAX - 1, 'n');
    const std::string host(KL_HOST_MAX - 1, 'h');
    KlipperPrinterCfg c = mkCfg(4294967295U, name.c_str(), host.c_str(), 65535);
    setOutcome(&c, KT_BAD_ANSWER, KL_LAYOUT_AFC, 255, 3, 4294967295UL);
    char rec[KL_RECORD_MAX];
    CHECK(klipperRecordEncode(c, rec, sizeof(rec)));
    KlipperPrinterCfg back;
    CHECK(decode(rec, &back));
    expectSame(c, back);
  }

  WHAT("what is no record");
  {
    const char* const bad[] = {
      "",
      "1",
      "0\t100001\tVoron 2.4\t192.168.10.50\t7125\t1\t3\t8\t2\t1793232000",      // another version
      "2\t100001\tVoron 2.4\t192.168.10.50\t7125\t1\t3\t8\t2\t1793232000",
      "1\t100001\tVoron 2.4\t192.168.10.50\t7125\t1\t3\t8\t2",                  // the time is missing
      "1\t100001\tVoron 2.4\t192.168.10.50\t7125",                              // everything after the port
      "1\t100001\tVoron 2.4",
      "1\t99999\tVoron 2.4\t192.168.10.50\t7125\t1\t3\t8\t2\t1793232000",       // an id below the base
      "1\t0\tVoron 2.4\t192.168.10.50\t7125\t1\t3\t8\t2\t1793232000",
      "1\t100001\tVoron 2.4\t192.168.10.50\t0\t1\t3\t8\t2\t1793232000",         // port 0
      "1\t100001\tVoron 2.4\t192.168.10.50\t65536\t1\t3\t8\t2\t1793232000",     // port past 65535
      "1\t100001\tVoron 2.4\t192.168.10.50\tabc\t1\t3\t8\t2\t1793232000",       // no number
      "1\t100001\t\t192.168.10.50\t7125\t1\t3\t8\t2\t1793232000",               // no name
      "1\t100001\tVoron 2.4\t\t7125\t1\t3\t8\t2\t1793232000",                   // no host
    };
    KlipperPrinterCfg out;
    for (int i = 0; i < COUNT(bad); i++) {
      if (decode(bad[i], &out)) {
        fprintf(stderr, "FAIL [%s]: decoded although it is no record: %s\n", g_what, bad[i]);
        abort();
      }
    }
    // The edges that are still records.
    CHECK(decode("1\t100000\tVoron 2.4\t192.168.10.50\t1\t1\t3\t8\t2\t1793232000", &out));
    CHECK_INT(out.id, 100000);
    CHECK_INT(out.port, 1);
    CHECK(decode("1\t100001\tVoron 2.4\t192.168.10.50\t65535\t1\t3\t8\t2\t1793232000", &out));
    CHECK_INT(out.port, 65535);
    CHECK_STR(out.name, "Voron 2.4");
    CHECK_STR(out.host, "192.168.10.50");
  }

  WHAT("the name: cleaned");
  {
    char r[KL_HOST_MAX];
    CHECK(nameClean("Voron 2.4", r));
    CHECK_STR(r, "Voron 2.4");
    CHECK(nameClean("  Voron 2.4  ", r));
    CHECK_STR(r, "Voron 2.4");
    CHECK(nameClean(" \t Voron 2.4 \n ", r));
    CHECK_STR(r, "Voron 2.4");
    // A control character or a tab inside is dropped (a tab would split the record).
    CHECK(nameClean("Vo\001ron\t2.4", r));
    CHECK_STR(r, "Voron2.4");
    CHECK(!nameClean("", r));
    CHECK(!nameClean("     ", r));
    CHECK(!nameClean(" \t\n ", r));
  }

  WHAT("the name: cut to 23 bytes, on the edge of a character");
  {
    char r[KL_HOST_MAX];
    // 26 bytes; the cut falls on the 23rd byte, between two letters.
    CHECK(nameClean("Vorön Trident Zweihundert", r));
    CHECK(validUtf8(r));
    CHECK(strlen(r) <= (size_t)(KL_NAME_MAX - 1));
    CHECK_STR(r, "Vorön Trident Zweihund");

    // A ü (two bytes) that would end on byte 24: it goes whole, not in half.
    const std::string a22(22, 'a');
    const std::string cut = a22 + "ü";
    CHECK(nameClean(cut.c_str(), r));
    CHECK(validUtf8(r));
    CHECK_STR(r, a22.c_str());
    // The same ü ending on byte 23 stays.
    const std::string a21(21, 'a');
    const std::string fits = a21 + "ü";
    CHECK_INT(fits.size(), 23);
    CHECK(nameClean(fits.c_str(), r));
    CHECK_STR(r, fits.c_str());
    // Three bytes, cut in the middle.
    const std::string euro = a21 + "€";
    CHECK(nameClean(euro.c_str(), r));
    CHECK(validUtf8(r));
    CHECK_STR(r, a21.c_str());
    // Plain letters: 23 stay, the 24th goes.
    const std::string a23(23, 'a');
    const std::string longer = a23 + "b";
    CHECK(nameClean(longer.c_str(), r));
    CHECK_STR(r, a23.c_str());
  }

  WHAT("the name: taken twice");
  {
    const char* const names[3] = {"Voron 2.4", "Trident", "Société"};
    CHECK(klipperNameTaken("Voron 2.4", names, 3, -1));
    CHECK(klipperNameTaken("voron 2.4", names, 3, -1));
    CHECK(klipperNameTaken("VORON 2.4", names, 3, -1));
    CHECK(!klipperNameTaken("Voron 2.5", names, 3, -1));
    CHECK(!klipperNameTaken("Voron", names, 3, -1));
    // The printer's own place does not count against it, another's does.
    CHECK(!klipperNameTaken("voron 2.4", names, 3, 0));
    CHECK(klipperNameTaken("voron 2.4", names, 3, 1));
    CHECK(klipperNameTaken("Trident", names, 3, 7));
    // Equal after the accents are folded.
    CHECK(klipperNameTaken("Societe", names, 3, -1));
    CHECK(klipperNameTaken("SOCIETE", names, 3, -1));
    // Only what count says is in the list.
    CHECK(!klipperNameTaken("Trident", names, 1, -1));
    CHECK(!klipperNameTaken("Voron 2.4", names, 0, -1));
  }

  WHAT("the address");
  {
    char r[KL_HOST_MAX];
    CHECK(hostClean("192.168.10.50", r));
    CHECK_STR(r, "192.168.10.50");
    CHECK(hostClean("  192.168.10.50  ", r));
    CHECK_STR(r, "192.168.10.50");
    CHECK(hostClean("http://192.168.10.50/", r));
    CHECK_STR(r, "192.168.10.50");
    CHECK(hostClean("http://voron.local//", r));
    CHECK_STR(r, "voron.local");
    CHECK(hostClean("voron.local/", r));
    CHECK_STR(r, "voron.local");
    CHECK(hostClean("http://voron.local", r));
    CHECK_STR(r, "voron.local");
    // No https: the scale talks plain http to Moonraker.
    CHECK(!hostClean("https://x", r));
    CHECK(!hostClean("https://voron.local/", r));
    CHECK(!hostClean("a b", r));
    CHECK(!hostClean("voron/printer", r));
    CHECK(!hostClean("", r));
    CHECK(!hostClean("   ", r));
    CHECK(!hostClean("http://", r));
    // 63 characters fit, 64 do not (the terminator is the 64th byte).
    const std::string h63(63, 'h');
    const std::string h64(64, 'h');
    CHECK(hostClean(h63.c_str(), r));
    CHECK_STR(r, h63.c_str());
    CHECK(!hostClean(h64.c_str(), r));
    const std::string with_scheme = "http://" + h63;
    CHECK(hostClean(with_scheme.c_str(), r));
    CHECK_STR(r, h63.c_str());
  }

  WHAT("the key");
  {
    char r[KL_HOST_MAX];
    CHECK(keyClean("abc_DEF-123", r));
    CHECK_STR(r, "abc_DEF-123");
    CHECK(keyClean("0123456789abcdef0123456789abcdef", r));
    // No key is allowed: it is how a key is taken away.
    CHECK(keyClean("", r));
    CHECK_STR(r, "");
    // Nothing that could end a header line or split the list "<id>=<key>;<id>=<key>".
    CHECK(!keyClean("abc;def", r));
    CHECK(!keyClean("abc=def", r));
    CHECK(!keyClean("a b", r));
    CHECK(!keyClean("abc\n", r));
    CHECK(!keyClean("äbc", r));
    // 47 characters fit, 48 do not.
    const std::string k47(47, 'k');
    const std::string k48(48, 'k');
    CHECK(keyClean(k47.c_str(), r));
    CHECK_STR(r, k47.c_str());
    CHECK(!keyClean(k48.c_str(), r));
  }

  WHAT("the list of keys");
  {
    char k[KL_KEY_MAX];
    char out[KL_KEYS_MAX];
    const char* keys = "100001=abc;100002=def";
    CHECK(klipperKeysGet(keys, 100001, k, sizeof(k)));
    CHECK_STR(k, "abc");
    CHECK(klipperKeysGet(keys, 100002, k, sizeof(k)));
    CHECK_STR(k, "def");
    CHECK(!klipperKeysGet(keys, 100003, k, sizeof(k)));
    CHECK(!klipperKeysGet("", 100001, k, sizeof(k)));
    // A number is matched whole, at its start and at its end.
    CHECK(!klipperKeysGet("1000011=zzz", 100001, k, sizeof(k)));
    CHECK(!klipperKeysGet("100001=abc", 10000, k, sizeof(k)));
    CHECK(!klipperKeysGet("1100001=abc;100002=def", 100001, k, sizeof(k)));

    // Set, add, change, take away.
    CHECK(klipperKeysPut("", 100001, "abc", out, sizeof(out)));
    CHECK_STR(out, "100001=abc");
    std::string cur = out;
    CHECK(klipperKeysPut(cur.c_str(), 100002, "def", out, sizeof(out)));
    CHECK_STR(out, "100001=abc;100002=def");
    cur = out;
    CHECK(klipperKeysPut(cur.c_str(), 100001, "xyz", out, sizeof(out)));
    CHECK(klipperKeysGet(out, 100001, k, sizeof(k)));
    CHECK_STR(k, "xyz");
    CHECK(klipperKeysGet(out, 100002, k, sizeof(k)));
    CHECK_STR(k, "def");
    const std::string changed = out;
    CHECK(changed.find("100001=") == changed.rfind("100001="));   // once, not twice
    CHECK(klipperKeysPut("100001=abc;100002=def", 100001, "", out, sizeof(out)));
    CHECK_STR(out, "100002=def");
    CHECK(klipperKeysPut("100001=abc;100002=def", 100002, "", out, sizeof(out)));
    CHECK_STR(out, "100001=abc");
    CHECK(klipperKeysPut("100002=def", 100002, "", out, sizeof(out)));
    CHECK_STR(out, "");
    CHECK(klipperKeysPut("100001=abc", 100005, "", out, sizeof(out)));
    CHECK_STR(out, "100001=abc");

    // The result must fit with its terminator: 21 characters need 22 bytes.
    char small[32];
    CHECK(klipperKeysPut("100001=abc", 100002, "def", small, 22));
    CHECK_STR(small, "100001=abc;100002=def");
    CHECK(!klipperKeysPut("100001=abc", 100002, "def", small, 21));
    CHECK(!klipperKeysPut("100001=abc", 100002, "def", small, 12));
  }

  WHAT("the list of keys, full");
  {
    // Fifty-five characters an entry with the separator: 58 of 47-character
    // keys fit 3200 bytes, the 59th does not, and the list stays as it was.
    const std::string key(47, 'k');
    std::string cur;
    int count = 0;
    for (;;) {
      char next[KL_KEYS_MAX];
      if (!klipperKeysPut(cur.c_str(), (uint32_t)(100001 + count), key.c_str(), next, sizeof(next))) break;
      cur = next;
      count++;
      CHECK(count <= 64);
    }
    CHECK_INT(count, 58);
    CHECK_INT(cur.size(), 55 * 58 - 1);
    char k[KL_KEY_MAX];
    CHECK(klipperKeysGet(cur.c_str(), 100001, k, sizeof(k)));
    CHECK_STR(k, key.c_str());
    CHECK(klipperKeysGet(cur.c_str(), 100058, k, sizeof(k)));
    CHECK_STR(k, key.c_str());
    CHECK(!klipperKeysGet(cur.c_str(), 100059, k, sizeof(k)));
  }

  WHAT("room for more printers");
  {
    CHECK_INT(klipperRoomLeft(0, 400), 42);
    CHECK_INT(klipperRoomLeft(60, 400), 4);
    CHECK_INT(klipperRoomLeft(0, 50), 0);    // less than the reserve: not a huge number
    CHECK_INT(klipperRoomLeft(0, 0), 0);
    CHECK_INT(klipperRoomLeft(0, 63), 0);
    CHECK_INT(klipperRoomLeft(0, 64), 0);
    CHECK_INT(klipperRoomLeft(0, 71), 0);
    CHECK_INT(klipperRoomLeft(0, 72), 1);
    CHECK_INT(klipperRoomLeft(0, 575), 63);
    CHECK_INT(klipperRoomLeft(0, 576), 64);
    CHECK_INT(klipperRoomLeft(0, 4000), 64);
    CHECK_INT(klipperRoomLeft(10, 576), 54);
    CHECK_INT(klipperRoomLeft(63, 4000), 1);
    CHECK_INT(klipperRoomLeft(64, 4000), 0);
  }

  WHAT("the next number is never one that was used");
  {
    const uint32_t two[2] = {100003, 100001};
    CHECK_INT(klipperNextId(0, two, 2), 100004);
    const uint32_t one[1] = {100003};
    CHECK_INT(klipperNextId(100010, one, 1), 100010);    // one that was deleted is not used again
    CHECK_INT(klipperNextId(0, nullptr, 0), 100001);
    CHECK_INT(klipperNextId(5, nullptr, 0), 100001);
    CHECK_INT(klipperNextId(100001, nullptr, 0), 100001);
    const uint32_t hi[1] = {100020};
    CHECK_INT(klipperNextId(100010, hi, 1), 100021);
  }
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp),
                    str(root / "src" / "services" / "klipper_store_rules.cpp"),
                    str(root / "src" / "services" / "text_util.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("test_klipper_store_rules: ok")
