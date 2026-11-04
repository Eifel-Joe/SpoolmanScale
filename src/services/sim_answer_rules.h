#pragma once

#include <stdint.h>
#include <string.h>

// ============================================================
//  A MADE-UP ANSWER OF THE SERVER (acceptance build, Spec 5.3 AA, v0.8.7-beta.2)
//
//  The refusals of a server - a key it turns down, an account it does not let
//  in, a rate limit with its wait - and a weight it would not take cannot be
//  had from Jörg's BamBuddy without harm, so the acceptance asks for them:
//  the next `left` requests to the scale's own server whose path holds `word`
//  (any request with an empty word) get `code`, without being sent. The rules
//  have no switch, the host test compiles them (test_acceptance_sim_rules.py);
//  the state and its lock are app/doc_build.cpp's, the door is BackendHttp.
// ============================================================

constexpr int     SIM_ANSWER_CODE_MIN  = 400;   // errors only: a made-up 200 would say nothing
constexpr int     SIM_ANSWER_CODE_MAX  = 599;
constexpr int     SIM_ANSWER_COUNT_MAX = 50;
constexpr size_t  SIM_ANSWER_WORD_MAX  = 39;    // characters of the path word
constexpr size_t  SIM_ANSWER_RETRY_MAX = 10;    // digits of the wait, as a header gives it

struct SimAnswer {
  int     code = 0;
  uint8_t left = 0;
  char    word[SIM_ANSWER_WORD_MAX + 1] = "";
  char    retry[SIM_ANSWER_RETRY_MAX + 1] = "";   // empty: no Retry-After
};

// A character a path word may hold: what a URL path and query carry, no blank.
inline bool simAnswerWordChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '/' || c == '-' || c == '_' || c == '.' || c == '?' || c == '=' || c == '&';
}

inline bool simAnswerArgsOk(int code, int count, const char* word, const char* retry) {
  if (code < SIM_ANSWER_CODE_MIN || code > SIM_ANSWER_CODE_MAX) return false;
  if (count < 1 || count > SIM_ANSWER_COUNT_MAX) return false;
  const char* w = word ? word : "";
  if (strlen(w) > SIM_ANSWER_WORD_MAX) return false;
  for (const char* p = w; *p; p++) if (!simAnswerWordChar(*p)) return false;
  const char* r = retry ? retry : "";
  if (strlen(r) > SIM_ANSWER_RETRY_MAX) return false;
  for (const char* p = r; *p; p++) if (*p < '0' || *p > '9') return false;
  return true;
}

// Only after simAnswerArgsOk().
inline void simAnswerSet(SimAnswer& a, int code, int count, const char* word, const char* retry) {
  a.code = code;
  a.left = (uint8_t)count;
  strncpy(a.word, word ? word : "", sizeof(a.word) - 1);
  a.word[sizeof(a.word) - 1] = '\0';
  strncpy(a.retry, retry ? retry : "", sizeof(a.retry) - 1);
  a.retry[sizeof(a.retry) - 1] = '\0';
}

inline void simAnswerClear(SimAnswer& a) { a = SimAnswer(); }

// Whether the request to `uri` gets the made-up answer; one fewer is left then.
inline bool simAnswerTake(SimAnswer& a, const char* uri) {
  if (a.left == 0 || !uri) return false;
  if (a.word[0] && !strstr(uri, a.word)) return false;
  a.left--;
  return true;
}
