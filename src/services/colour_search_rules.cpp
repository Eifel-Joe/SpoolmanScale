#include "services/colour_search_rules.h"

#include "services/text_fold.h"

// Whether the name, from at, starts with the whole word. Both are read one
// character at a time through the fold, so a capital umlaut meets the small
// one and a sequence is never taken apart.
static bool startsHere(const char* at, const char* word) {
  while (*word) {
    if (!*at) return false;   // the name ended inside the word
    uint32_t a, w;
    at = textFoldNext(at, &a);
    word = textFoldNext(word, &w);
    if (a != w) return false;
  }
  return true;
}

bool colourSearchMatch(const char* name, const char* word) {
  if (!word || !word[0]) return true;
  if (!name) return false;
  // From every character of the name: a part that matched and broke off
  // starts over one character further on, so "bbone" holds "bone". One
  // character, not one byte: a start inside a sequence would read its
  // continuation bytes as letters of their own.
  for (const char* at = name; *at; ) {
    if (startsHere(at, word)) return true;
    uint32_t skipped;
    at = textFoldNext(at, &skipped);
  }
  return false;
}
