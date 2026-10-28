#include "services/colour_search_rules.h"

// A capital A-Z as its small letter, every other byte as it is. A byte beyond
// ASCII is below 'A' where char is signed and above 'Z' where it is not: left
// alone either way.
static char fold(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool colourSearchMatch(const char* name, const char* word) {
  if (!word || !word[0]) return true;
  if (!name) return false;
  // From every byte of the name: a part that matched and broke off starts
  // over one byte further on, so "bbone" holds "bone".
  for (const char* at = name; *at; at++) {
    size_t k = 0;
    while (word[k] && at[k] && fold(at[k]) == fold(word[k])) k++;
    if (!word[k]) return true;
  }
  return false;
}
