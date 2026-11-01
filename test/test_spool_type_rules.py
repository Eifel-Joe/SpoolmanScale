"""Compile the production spool type rules and check which empty spool a reading points at: the types of one vendor, the nearest ones to a weight, what a full spool says about its tare, and when the scale asks first."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/spool_type_rules.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>

// The limits, spelled out here on purpose: the production defines must agree
// with this text, not with themselves. A name of 63 bytes and a terminator,
// "near" is 10 g either side and shows at most five types, a stored tare that
// is 5 g off is still the same spool, 0.5 g is the noise of an unused place,
// and a new spool's tare is 30 to 600 g (from app_config.h).
static_assert(SPOOL_TYPE_NAME_MAX == 64, "63 bytes and the terminator");
static_assert(SPOOL_TYPE_NEAR_G == 10.0f, "10 g either side");
static_assert(SPOOL_TYPE_NEAR_MAX == 5, "five types at most");
static_assert(SPOOL_TYPE_DEVIATION_G == 5.0f, "5 g off is the same spool");
static_assert(SPOOL_TYPE_UNUSED_G == 0.5f, "0.5 g is noise");
static_assert(NEW_SPOOL_TARE_MIN_G == 30.0f, "a tare below 30 g is no spool");
static_assert(NEW_SPOOL_TARE_MAX_G == 600.0f, "a tare above 600 g is no spool");
static_assert(BB_CAP_TOLERANCE_G == 2.0f, "2 g over the label is still full");

// True when the first n indices are exactly the ones wanted, in that order.
static bool idxIs(const int* got, int n, std::initializer_list<int> want) {
  if (n != (int)want.size()) return false;
  int i = 0;
  for (int w : want) {
    if (got[i++] != w) return false;
  }
  return true;
}

// Eight slots that read -1: a slot still -1 afterwards was not written.
static void clearOut(int* out) {
  for (int i = 0; i < 8; i++) out[i] = -1;
}

// Two weights agree to within a hundredth of a gram (the floats of a reading
// like 1212.3 are not exact).
static bool closeTo(float a, float b) {
  return std::fabs(a - b) < 0.01f;
}

int main() {
  const SpoolType cat[7] = {
    {"Bambu Lab - Plastic High Temp", 216},
    {"Bambu Lab - Plastic Low Temp", 250},
    {"Bambu Lab - White", 253},
    {"Tinmorry black Spool", 167},
    {"Tinmorry grey Spool", 200},
    {"Sunlu Plastic", 160},
    {"Elegoo Cardboard", 205},
  };
  // Six types of one vendor within 3 g of each other: with 250 g asked for,
  // all six are near and only the five closest fit.
  const SpoolType dense[6] = {
    {"Bambu Lab - A", 250}, {"Bambu Lab - B", 251}, {"Bambu Lab - C", 249},
    {"Bambu Lab - D", 252}, {"Bambu Lab - E", 248}, {"Bambu Lab - F", 253},
  };
  // Six types of one vendor in the order of the list from far to near: with
  // 250 g asked for, the closest one stands last.
  const SpoolType dense2[6] = {
    {"Bambu Lab - A", 258}, {"Bambu Lab - B", 257}, {"Bambu Lab - C", 256},
    {"Bambu Lab - D", 255}, {"Bambu Lab - E", 254}, {"Bambu Lab - F", 250},
  };
  int out[8];

  // A type belongs to a vendor when the name starts with it, in any case and
  // with blanks around the vendor ignored, and the prefix ends at the end of
  // the name, a blank or a dash: "Tin" is not "Tinmorry". Nothing belongs to
  // an empty vendor.
  assert(spoolTypeOfVendor("Tinmorry grey Spool", "tinmorry"));
  assert(spoolTypeOfVendor("Bambu Lab - Plastic Low Temp", " BAMBU LAB "));
  assert(!spoolTypeOfVendor("Tinmorry grey Spool", "Tin"));
  assert(!spoolTypeOfVendor("Sunlu Plastic", "Bambu Lab"));
  assert(!spoolTypeOfVendor("Bambu Lab - White", ""));

  // The types of one vendor, in the order of the list, and not more than the
  // room given (the third Bambu Lab type has no slot when there are two).
  int n = spoolTypeFilter(cat, 7, "Tinmorry", out, 8);
  assert(n == 2);
  assert(idxIs(out, n, {3, 4}));
  n = spoolTypeFilter(cat, 7, "bambu lab", out, 8);
  assert(n == 3);
  assert(idxIs(out, n, {0, 1, 2}));
  n = spoolTypeFilter(cat, 7, "Overture", out, 8);
  assert(n == 0);
  clearOut(out);
  n = spoolTypeFilter(cat, 7, "Bambu Lab", out, 2);
  assert(n == 2);
  assert(out[2] == -1);

  // The types of that vendor within 10 g of the reading, the closest first.
  // 250 and 253 are 3 and 6 g from 247; 216 is far off.
  n = spoolTypeNear(cat, 7, {"Bambu Lab", 247.0f, SPOOL_TYPE_NEAR_G}, out, SPOOL_TYPE_NEAR_MAX);
  assert(n == 2);
  assert(idxIs(out, n, {1, 2}));
  // Exactly 10 g away still counts (250 - 240), 253 at 13 g does not.
  n = spoolTypeNear(cat, 7, {"Bambu Lab", 240.0f, 10.0f}, out, SPOOL_TYPE_NEAR_MAX);
  assert(n == 1);
  assert(idxIs(out, n, {1}));
  n = spoolTypeNear(cat, 7, {"Tinmorry", 203.4f, 10.0f}, out, SPOOL_TYPE_NEAR_MAX);
  assert(n == 1);
  assert(idxIs(out, n, {4}));
  // A vendor without a type in the list is looked for in all of them: 205 g
  // is 2 g from 203, 200 g is 3 g, so the cardboard comes first.
  n = spoolTypeNear(cat, 7, {"Overture", 203.0f, 10.0f}, out, SPOOL_TYPE_NEAR_MAX);
  assert(n == 2);
  assert(idxIs(out, n, {6, 4}));
  // At the same distance the order of the list decides (250 first, then 251
  // and 249 which are both 1 g away, then 252 and 248 at 2 g), and the
  // sixth type has no room.
  clearOut(out);
  n = spoolTypeNear(dense, 6, {"Bambu Lab", 250.0f, 10.0f}, out, 5);
  assert(n == 5);
  assert(idxIs(out, n, {0, 1, 2, 3, 4}));
  assert(out[5] == -1);
  // The five nearest, not the first five of the list: F is 0 g away and
  // stands last, so it comes first, and A (8 g, the farthest) has no room.
  clearOut(out);
  n = spoolTypeNear(dense2, 6, {"Bambu Lab", 250.0f, SPOOL_TYPE_NEAR_G}, out, 5);
  assert(n == 5);
  assert(idxIs(out, n, {5, 4, 3, 2, 1}));
  assert(out[5] == -1);
  // With a tolerance of a kilogram the nearest type is still one: 167 g is
  // 13 g from 180, 200 g is 20 g.
  clearOut(out);
  n = spoolTypeNear(cat, 7, {"Tinmorry", 180.0f, 1000.0f}, out, 1);
  assert(n == 1);
  assert(idxIs(out, n, {3}));
  assert(out[1] == -1);
  // No weight, no type.
  n = spoolTypeNear(cat, 7, {"Tinmorry", 0.0f, 10.0f}, out, SPOOL_TYPE_NEAR_MAX);
  assert(n == 0);

  // A full spool on the scale with nothing on it but the spool: what is over
  // the label weight is the tare. 1212 g with 1000 g of filament is 212 g.
  assert(spoolTypeTareFromFull(1212.0f, 1000.0f) == 212.0f);
  // A tare is 30 to 600 g (both ends in), and not a number is none.
  assert(!spoolTypeTarePlausible(29.9f));
  assert(spoolTypeTarePlausible(30.0f));
  assert(spoolTypeTarePlausible(600.0f));
  assert(!spoolTypeTarePlausible(600.1f));
  assert(!spoolTypeTarePlausible(NAN));

  // The weight a full spool is labelled with: the larger of the label weight
  // and what is left, so a spool filled over its label is not cut to it.
  assert(spoolTypeFullLabel(1000.0f, 980.0f) == 1000.0f);
  assert(spoolTypeFullLabel(1000.0f, 1012.4f) == 1012.4f);

  // What a full spool on the scale writes, in the whole grams the server
  // keeps: the tare as it will be stored, the label weight, and a rest that
  // never lands above that label. The server takes the rest as the reading
  // less the tare it holds, so the rest is worked out against the rounded
  // tare. Each line is (bare reading, filament, tare) and what it writes.
  // 1212.3 g bare, 1000 g of filament, a measured tare of 212.3 g: the server
  // keeps 212 g, so the rest is 1000.3 g, the label rounds to 1000 g, and the
  // rest is cut to it (not 1000.3 g, which the server would see above its
  // label).
  FullSpoolWrite w = spoolTypeFullSpoolWrite(1212.3f, 1000.0f, 212.3f);
  assert(w.tare_g == 212);
  assert(w.label_g == 1000);
  assert(closeTo(w.rest_g, 1000.0f));
  // A tare of 200 g under the same reading: 1012.3 g are left, over the
  // filament, so the label is lifted to 1012 g and the rest is cut to that.
  w = spoolTypeFullSpoolWrite(1212.3f, 1000.0f, 200.0f);
  assert(w.tare_g == 200);
  assert(w.label_g == 1012);
  assert(closeTo(w.rest_g, 1012.0f));
  // 1212.6 g with a tare of 212.6 g stores 213 g, which leaves 999.6 g, under
  // the label of 1000 g and so kept as it is.
  w = spoolTypeFullSpoolWrite(1212.6f, 1000.0f, 212.6f);
  assert(w.tare_g == 213);
  assert(w.label_g == 1000);
  assert(closeTo(w.rest_g, 999.6f));
  // A reading below the tare leaves nothing, and the label stays the filament.
  w = spoolTypeFullSpoolWrite(150.0f, 1000.0f, 200.0f);
  assert(w.tare_g == 200);
  assert(w.label_g == 1000);
  assert(closeTo(w.rest_g, 0.0f));
  // A reading that is not a number leaves nothing as well, and the label is
  // the filament rounded (999.6 g to 1000 g).
  w = spoolTypeFullSpoolWrite(NAN, 999.6f, 212.0f);
  assert(w.tare_g == 212);
  assert(w.label_g == 1000);
  assert(closeTo(w.rest_g, 0.0f));
  // A rest exactly on the label stays on it.
  w = spoolTypeFullSpoolWrite(1200.0f, 1000.0f, 200.0f);
  assert(w.tare_g == 200);
  assert(w.label_g == 1000);
  assert(closeTo(w.rest_g, 1000.0f));

  // Whether a weighing asks before it is written: (BamBuddy, label weight,
  // rest). BamBuddy keeps no rest above the label, in its own inventory and
  // behind Spoolman alike, so a rest more than 2 g over a known label asks to
  // raise it. Spool 19 on 25.10.2026: 1035.9 g against a 1000 g label was
  // stored as 1000 g without a word.
  assert(spoolLabelAsk(true, 1000.0f, 1035.9f));
  assert(spoolLabelAsk(true, 1015.0f, 1019.2f));
  assert(spoolLabelAsk(true, 1000.0f, 1002.1f));
  // Up to 2 g over is a full spool, not a mislabelled one.
  assert(!spoolLabelAsk(true, 1000.0f, 1002.0f));
  assert(!spoolLabelAsk(true, 1000.0f, 980.0f));
  // No label known, or no reading: nothing to compare.
  assert(!spoolLabelAsk(true, 0.0f, 1200.0f));
  assert(!spoolLabelAsk(true, 1000.0f, NAN));
  // Other servers stay as they were: Spoolman on its own and FilaMan never
  // asked, and nothing here measured them.
  assert(!spoolLabelAsk(false, 1000.0f, 1035.9f));

  // What the scale asks first: (label weight, filament left, the bare
  // reading, whether the spool has a tare of its own, the tare stored for it).
  // A full spool that has no tare of its own, or one that is more than 5 g off
  // the reading, suggests the tare it just read (the stored 250 g is only the
  // pre-set one); within 5 g (1205 - 1000 - 200) it is the same spool.
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1200.0f, false, 250.0f) == STA_SUGGEST);
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1200.0f, true, 250.0f) == STA_SUGGEST);
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1205.0f, true, 200.0f) == STA_NONE);
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1206.0f, true, 200.0f) == STA_SUGGEST);
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1197.0f, true, 200.0f) == STA_NONE);
  // A tare read off a full spool that is below 30 g or above 600 g is no spool.
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1010.0f, false, 250.0f) == STA_IMPLAUSIBLE);
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1700.0f, false, 250.0f) == STA_IMPLAUSIBLE);
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 1030.0f, false, 250.0f) == STA_SUGGEST);
  // A spool that is not full says nothing about its tare: the list of types,
  // unless it has a tare of its own.
  assert(spoolTypeFirstAsk(1000.0f, 260.0f, 460.0f, false, 250.0f) == STA_LIST);
  assert(spoolTypeFirstAsk(1000.0f, 260.0f, 460.0f, true, 250.0f) == STA_NONE);
  // A bare reading of NEW_SPOOL_TARE_MIN_G (30 g, the only value pinned here)
  // is the dish with nothing on it: nothing to ask.
  assert(spoolTypeFirstAsk(1000.0f, 1000.0f, 30.0f, false, 250.0f) == STA_NONE);
  // Full is within 0.5 g of the label (999.6), 0.6 g short is not (999.4); and
  // with no label weight at all there is nothing to take the tare from.
  assert(spoolTypeFirstAsk(1000.0f, 999.6f, 1200.0f, false, 250.0f) == STA_SUGGEST);
  assert(spoolTypeFirstAsk(1000.0f, 999.4f, 1200.0f, false, 250.0f) == STA_LIST);
  assert(spoolTypeFirstAsk(0.0f, 0.0f, 1200.0f, false, 250.0f) == STA_LIST);

  // A name loses the blanks on both sides and is cut to what fits, never in
  // the middle of a UTF-8 character: 62 letters and a two-byte u-umlaut come
  // to 64 bytes and only 63 fit, so the umlaut goes; with 61 letters it fits.
  char buf[SPOOL_TYPE_NAME_MAX];
  spoolTypeTrimName(buf, SPOOL_TYPE_NAME_MAX, "  Test Spule  ");
  assert(strcmp(buf, "Test Spule") == 0);
  const std::string umlaut62 = std::string(62, 'a') + "\xc3\xbc";
  spoolTypeTrimName(buf, SPOOL_TYPE_NAME_MAX, umlaut62.c_str());
  assert(strlen(buf) == 62);
  const std::string umlaut61 = std::string(61, 'a') + "\xc3\xbc";
  spoolTypeTrimName(buf, SPOOL_TYPE_NAME_MAX, umlaut61.c_str());
  assert(strlen(buf) == 63);

  // A name is taken when the list has it, whatever the case and the blanks
  // around it. A longer name is another one, and no name is never taken.
  assert(spoolTypeNameTaken(cat, 7, "tinmorry GREY spool"));
  assert(spoolTypeNameTaken(cat, 7, "  Tinmorry grey Spool "));
  assert(!spoolTypeNameTaken(cat, 7, "Tinmorry grey Spool 2"));
  assert(!spoolTypeNameTaken(cat, 7, ""));

  // Whole grams, rounded; not a number and negative are 0.
  assert(spoolTypeGrams(199.5f) == 200);
  assert(spoolTypeGrams(199.4f) == 199);
  assert(spoolTypeGrams(-3.0f) == 0);
  assert(spoolTypeGrams(NAN) == 0);

  // U (V25): a type chosen for the spool goes before the chain and is trusted,
  // also when it says Bambu's fixed 250 g; without one the chain stands as it is.
  bool t = false;
  assert(spoolTypeCreateTare(167, 250, false, &t) == 167 && t);
  assert(spoolTypeCreateTare(250, 250, false, &t) == 250 && t);
  assert(spoolTypeCreateTare(0, 250, false, &t) == 250 && !t);
  assert(spoolTypeCreateTare(0, 200, true, &t) == 200 && t);
  assert(spoolTypeCreateTare(0, 0, false, &t) == 0 && !t);
  assert(spoolTypeCreateTare(-5, 200, true, &t) == 200 && t);
  assert(spoolTypeCreateTare(167, 250, false, nullptr) == 167);
  // L21: an inherited empty spool is not written onto the new one.
  assert(spoolTypeOwnCore(250, true) == 0);
  assert(spoolTypeOwnCore(167, false) == 167);
  assert(spoolTypeOwnCore(0, false) == 0);
  assert(spoolTypeOwnCore(-3, false) == 0);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "spool_type_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("spool type rules: ok")
