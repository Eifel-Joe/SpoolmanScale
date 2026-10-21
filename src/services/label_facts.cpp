#include "services/label_facts.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lang.h"
#include "services/backend.h"

static bool hasText(const char* s) { return s && s[0]; }

// Whether a colour is six hex digits, which the label writes with its '#'.
static bool isHex6(const char* s) {
  if (strlen(s) != 6) return false;
  for (int i = 0; i < 6; i++)
    if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}

// The date as the layout wants it: the first use where the backend
// recorded one, else the day the spool was added; or always the latter.
// False when the backend gave neither.
static bool dateFact(const SpoolLabelData& spool, const LabelLayout& layout, LabelFact* out) {
  const bool added_only = labelLayoutOption(layout, LO_DATE_ADDED);
  if (!added_only && spool.first_used[0]) { *out = LabelFact{ T(STR_LBL_L_FIRST), spool.first_used }; return true; }
  if (spool.added[0]) { *out = LabelFact{ T(STR_LBL_L_ADDED), spool.added }; return true; }
  return false;
}

// Appends a fact while the list has room; the value is copied in.
static void addFact(LabelFactList* l, int capacity, const LabelFact& f) {
  if (l->n >= capacity || l->n >= LABEL_MAX_FACTS) return;
  const char* value = nullptr;
  if (hasText(f.value)) {
    snprintf(l->values[l->n], sizeof(l->values[0]), "%s", f.value);
    value = l->values[l->n];
  }
  char* line = l->lines[l->n];
  const size_t size = sizeof(l->lines[0]);
  if (hasText(f.caption) && value) snprintf(line, size, "%s  %s", f.caption, value);
  else snprintf(line, size, "%s", hasText(f.caption) ? f.caption : (value ? value : ""));
  l->facts[l->n] = LabelFact{ f.caption, value };
  l->line_ptrs[l->n] = line;
  l->n++;
}

void labelSpoolFacts(const LabelLayout& layout, const SpoolLabelData& spool, int capacity,
                     LabelFactList* out) {
  *out = LabelFactList{};
  // What stays true for the spool's whole life: where it is kept, the colour
  // and when it came into use. The rest and the place change, and a label
  // that shows them is wrong a week later (Nikolai, 25.09.2026).
  char id[12];
  char color[sizeof(spool.color) + 1];
  snprintf(id, sizeof(id), "#%d", spool.id);
  snprintf(color, sizeof(color), isHex6(spool.color) ? "#%s" : "%s", spool.color);
  // The compact arrangement moves the maker down here, as the first fact.
  if (layout.preset == LABEL_PRESET_COMPACT && labelLayoutHas(layout, LF_VENDOR) &&
      spool.vendor[0])
    addFact(out, capacity, LabelFact{ nullptr, spool.vendor });
  if (labelLayoutHas(layout, LF_SPOOL_ID)) addFact(out, capacity, LabelFact{ backendName(), id });
  if (labelLayoutHas(layout, LF_COLOR) && spool.color[0])
    addFact(out, capacity, LabelFact{ T(STR_LBL_L_COLOR), color });
  if (labelLayoutHas(layout, LF_ARTICLE) && spool.article[0])
    addFact(out, capacity, LabelFact{ T(STR_LBL_ARTICLE_NO_SHORT), spool.article });
  LabelFact date;
  if (labelLayoutHas(layout, LF_DATE) && dateFact(spool, layout, &date)) addFact(out, capacity, date);
  if (labelLayoutHas(layout, LF_BRAND)) addFact(out, capacity, LabelFact{ "SpoolmanScale", nullptr });
}
