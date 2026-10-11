#pragma once

#include <gemini.h>

#include <cstddef>
#include <cstdint>

// Apps > AI's device half (X4 Pro), on the loop task: the key file on the card, this session's
// questions and their topic, the remembered daily limits, and one question asked - the Wikipedia
// lookup, then Google's model ladder - on a station the caller has already joined (it never starts
// the radio). The rules are lib/Gemini's and lib/WikiLookup's, shared with the WiPhone firmware; the
// wire is network/AiHttps. Used by the app's screens (activities/apps/Ai*) and the bench console
// (AIASK, AIKEY).
//
// THE KEY. gemini.txt in the card's "API Keys" folder, both names matched in any case, read again on
// every app open and before every question (a new key works at once). Only a GOOD read replaces the
// key in use: a card that fails a read keeps the last good one. ⚠ The key is never logged, drawn or
// printed: KeyInfo carries its length and gemini.h's config line (model, thinking, fallback, lookup,
// warnings), nothing else.
//
// THE CHAT is RAM only (PSRAM): the questions of this session, a failed one marked. What Google is
// sent with a question is the current TOPIC - its last GEM_CONTEXT answered exchanges since a topic
// break ("New topic", or more than two hours since the last question). The remembered daily limits
// (a model used up until Google's RetryInfo time) are kept on the card, /.crosspoint/ai/quota.txt
// (UTC deadlines only), so a reboot does not spend a question finding out again.
//
// ONE QUESTION (ask): a trusted clock's date and zone for the system instruction (SETTINGS says the
// clock was synced; otherwise no date at all), the question's keywords - never the question - looked
// up once on Wikipedia (unless gemini.txt says lookup=off; any failure is one log line and no
// background), then requests in a loop as geminiLadderNext says: retries, the fallback, the daily
// limits, and "Retry if it times out" (SETTINGS.aiRetryTimeouts, on unless set off) - one more try on
// the same model, then the fallback once, with the background already found. The result is folded
// into the chat - a Cancel too, as a failed entry reading "Cancelled" (as the WiPhone keeps one).
namespace AiRun {

enum class KeyState : uint8_t {
  NotRead,     // not looked for yet
  None,        // no gemini.txt in "API Keys", or no key line in it
  Ok,          // a key
  Refused,     // a key line that cannot be sent (KeyInfo::line says why)
  Unreadable,  // the card would not give the file up (a last good key, if any, is kept)
};

struct KeyInfo {
  KeyState state = KeyState::NotRead;
  bool usable = false;  // a key a question can use (this read's, or the last good one)
  int length = 0;       // the usable key's length
  char path[96] = "";   // the file read ("" when none was found)
  char line[256] = "";  // geminiConfigLine: never the key
  char model[GEM_MODEL_MAX] = "";
  char fallback[GEM_MODEL_MAX] = "";  // "" = none
  int thinking = 0;
  bool lookup = true;  // the Wikipedia lookup (gemini.txt lookup=off: false)
};

// Reads gemini.txt now (the card; loop task).
void reloadKey();
const KeyInfo& keyInfo();

// This session's questions, oldest first (nullptr when there is no memory for them).
const GeminiChat* chat();
// "New topic": the next question starts one (Gemini is sent nothing earlier). False when there was
// nothing to forget.
bool newTopic();
// The next question would be sent earlier exchanges (a topic is running).
bool topicRunning();

bool retryTimeouts();
void setRetryTimeouts(bool on);  // saved with the settings

// "Daily free limit for Flash reached - using Flash-Lite until about 17:00" when the configured model
// is remembered as used up for today; 0 and "" when it is not.
size_t quotaNote(char* out, size_t cap);

struct Progress {
  const char* stage = "";          // "Looking up Wikipedia...", "Asking Gemini...", a retry's words
  char model[GEM_LABEL_MAX] = "";  // the model being asked ("Flash"), "" before the ladder starts
  uint32_t startMs = 0;            // when the question was asked
  uint32_t waitUntilMs = 0;        // a retry's wait ends then (0 = not waiting)
  int attempts = 0;                // requests made so far
};

struct Hooks {
  // Between waits' slices (at least every AiHttps::SLICE_MS): false = the user cancelled.
  bool (*tick)(void* ctx) = nullptr;
  // The stage, the model or the wait changed.
  void (*changed)(const Progress& progress, void* ctx) = nullptr;
  void* ctx = nullptr;
};

struct Outcome {
  bool ok = false;
  bool cancelled = false;
  int exchange = -1;               // the chat entry the question became, a cancelled one too (-1: no memory)
  char label[GEM_LABEL_MAX] = "";  // the model that answered ("Flash-Lite")
  char fail[256] = "";             // why there is no answer
  int attempts = 0;
  int lastStatus = 0;
  int net = 0;  // GEM_NET_* of the last attempt
  uint32_t ms = 0;
  bool lookupAsked = false;  // the Wikipedia GET was made
  int lookupPages = 0;       // pages that went with the question
  uint32_t lookupMs = 0;
  char lookupWhy[64] = "";  // why no background went ("" when it did, or no lookup was due)
};

// One question, on a joined station; blocks the loop task (the ladder's waits included: minutes at
// most), the hooks called throughout.
void ask(const char* question, const Hooks& hooks, Outcome& out);

// A question that could not be sent (no Wi-Fi joined), into the chat as failed with `why`; its
// index, or -1.
int addFailed(const char* question, const char* why);

}  // namespace AiRun
