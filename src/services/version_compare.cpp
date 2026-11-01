#include "version_compare.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {
// How the beta suffix ranks, derived from the actual release history:
//
//   v0.5.7-beta      public release
//   v0.5.7-beta.1    internal build after it
//   v0.5.7-beta.2    internal build after that
//   v0.5.8-beta      next public release
//
// So a counter means "later than the public release of the same X.Y.Z", and a
// tag with no "beta" at all is a final release that outranks every beta.
//
// The previous implementation stopped parsing at the patch number, which made
// all four tags above compare equal. Nothing inside one X.Y.Z ever counted as
// an update, which is why the pre-release path looked like it worked and never
// actually offered anything.
constexpr uint64_t BETA_PUBLIC = 0;    // "-beta", no counter
constexpr uint64_t BETA_FINAL  = 999;  // no "beta" in the tag at all
constexpr int BETA_COUNTER_MAX = 998;

constexpr uint64_t WEIGHT_MAJOR = 1000000000ULL;
constexpr uint64_t WEIGHT_MINOR = 1000000ULL;
constexpr uint64_t WEIGHT_PATCH = 1000ULL;

// True when s ends with tail. Used for the two suffixes of a build made with a
// switch, which sit at the very end of the tag ("v0.8.6-beta.14-abnahme"); a
// match anywhere else in the string is no suffix.
bool endsWith(const char* s, const char* tail) {
  const size_t n = strlen(s);
  const size_t t = strlen(tail);
  return n >= t && strcmp(s + n - t, tail) == 0;
}
}  // namespace

uint64_t parseVersion(const char* v) {
  if (!v) return 0;

  // Skip a leading "v" or any other prefix and start at the first digit.
  const char* p = v;
  while (*p && !isdigit((unsigned char)*p)) p++;

  int major = 0, minor = 0, patch = 0;
  sscanf(p, "%d.%d.%d", &major, &minor, &patch);

  uint64_t beta = BETA_FINAL;
  const char* b = strstr(v, "beta");
  if (b) {
    beta = BETA_PUBLIC;
    b += 4;  // strlen("beta")
    if (*b == '.') {
      int n = 0;
      if (sscanf(b + 1, "%d", &n) == 1 && n > 0 && n <= BETA_COUNTER_MAX) {
        beta = (uint64_t)n;
      }
    }
  }

  if (major < 0) major = 0;
  if (minor < 0) minor = 0;
  if (patch < 0) patch = 0;

  const uint64_t value = (uint64_t)major * WEIGHT_MAJOR
                       + (uint64_t)minor * WEIGHT_MINOR
                       + (uint64_t)patch * WEIGHT_PATCH
                       + beta;

  // A build made with a switch ("-abnahme" the acceptance firmware, "-doku"
  // the documentation firmware; neither is ever released) ranks one below the
  // version it was built on, which gives two things at once. Next to the
  // published release before it (v0.8.6-beta.14-abnahme beside beta.13) it
  // ranks equal, so the update check offers nothing and never pulls the scale
  // back to an older beta. Next to its own release (beta.14) it ranks lower,
  // so the scale finds its way back as soon as that is out. Only these two
  // suffixes, at the end of the tag: "-beta" is no suffix, and an unknown
  // ending such as "-foo" ranks like the plain version. Zero stays zero: one
  // below the lowest value must not wrap around to the highest number there is.
  if ((endsWith(v, "-abnahme") || endsWith(v, "-doku")) && value > 0) return value - 1;
  return value;
}
