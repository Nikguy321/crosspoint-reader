#include "AiRun.h"

#include <Arduino.h>
#include <HalClock.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <tz_grid.h>
#include <wiki_lookup.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "AiHttps.h"
#include "CrossPointSettings.h"
#include "activities/apps/AiText.h"
#include "sleepcards/CardTime.h"
#include "util/TaskWatchdog.h"
#include "util/Timezones.h"

namespace AiRun {
namespace {

constexpr char QUOTA_DIR[] = "/.crosspoint/ai";
constexpr char QUOTA_FILE[] = "/.crosspoint/ai/quota.txt";
constexpr size_t KEY_READ_MAX = 2048;
constexpr size_t QUOTA_READ_MAX = 512;
// Gemini's response: a 4096-token answer is ~16 KB of escaped JSON (the WiPhone's sink is 64 KB too).
constexpr size_t SINK_CAP = 64 * 1024;
// The answer's parts joined, before geminiCleanText cuts it to GEM_A_MAX.
constexpr size_t TEXT_CAP = 24 * 1024;
// The request: at most GEM_SEND_TEXT_CAP of text (escaped, at worst doubled), the instruction and
// the background.
constexpr size_t BODY_CAP = 32 * 1024;
// Gemini: TCP and TLS in 10 s, then up to 150 s for the answer's headers (a non-streamed answer's
// headers come once it is all written; free-tier Flash takes 30-90 s), and a body that stops for 30 s
// is a dead socket (it comes in one go).
constexpr uint32_t GEMINI_DNS_MS = 10000;
constexpr uint32_t GEMINI_CONNECT_MS = 10000;
constexpr uint32_t GEMINI_FIRST_BYTE_MS = 150000;
constexpr uint32_t GEMINI_BODY_IDLE_MS = 30000;
// The Wikipedia lookup: one GET, 20 s for all of it, never retried.
constexpr uint32_t WIKI_CONNECT_MS = 10000;
constexpr uint32_t WIKI_TOTAL_MS = 20000;
constexpr uint32_t WIKI_IDLE_MS = 5000;
constexpr char GEMINI_USER_AGENT[] =
    "CrossPoint-X4Pro/" CROSSPOINT_VERSION " (+https://github.com/Nikguy321/crosspoint-reader)";
constexpr char ASKING[] = "Asking Gemini...";

// Everything big, in PSRAM, allocated on first use and kept (the chat lives as long as the boot).
// Plain C structs only: zeroed, then initialised by gemini.h's own calls.
struct State {
  GeminiChat chat;
  GeminiConfig good;    // ⚠ the last good key file (the key in it)
  GeminiConfig parsed;  // ⚠ the file just read, wiped after
  bool haveGood;
  GeminiSession session;
  GeminiQuota quota;
  GeminiQuota quotaIn;   // as the question found it (a change is saved)
  GeminiQuota quotaTmp;  // the card's copy while it loads
  bool quotaLoaded;
  GeminiLadder ladder;
  GeminiReply reply;
  WikiResult wiki;
  char question[GEM_Q_MAX];
  char kw[WIKI_KW_CAP];
  char wikiUrl[WIKI_URL_MAX];
  char bg[WIKI_BG_MAX + 1];
  char src[WIKI_SRC_MAX + 1];
  char sys[GEM_SYS_MAX];
  char url[sizeof(GEM_URL_PREFIX) + GEM_MODEL_MAX + sizeof(GEM_URL_SUFFIX)];
  char label[GEM_LABEL_MAX];
  char primaryLabel[GEM_LABEL_MAX];
  char stage[GEM_LABEL_MAX + 160];
  char answer[GEM_A_MAX];
  char keyFile[KEY_READ_MAX + 1];  // ⚠ the key file's bytes while it is parsed, wiped after
  char text[TEXT_CAP];
  char body[BODY_CAP];
  char sink[SINK_CAP];
};

HalMemory::PsramBuffer stateBuffer;
State* st = nullptr;
KeyInfo key;
KeyState lastLogged = KeyState::NotRead;

State* state() {
  if (st != nullptr) return st;
  stateBuffer = HalMemory::allocatePsram(sizeof(State));
  if (!stateBuffer) {
    LOG_ERR("AI", "No PSRAM for the AI app (%u bytes)", static_cast<unsigned>(sizeof(State)));
    return nullptr;
  }
  std::memset(stateBuffer.get(), 0, sizeof(State));
  st = reinterpret_cast<State*>(stateBuffer.get());
  geminiChatClear(&st->chat);
  geminiSessionInit(&st->session);
  geminiQuotaClear(&st->quota);
  return st;
}

// The clock's UTC when it has been set from the internet; 0 otherwise (no date beats a wrong one).
int64_t trustedUtc() {
  if (!SETTINGS.clockHasBeenSynced || !halClock.isAvailable()) return 0;
  time_t utc = 0;
  if (!halClock.utcEpoch(utc) || !sleepcards::plausibleTime(static_cast<int64_t>(utc))) return 0;
  return static_cast<int64_t>(utc);
}

// The clock zone's offset now and its word for the date sentence ("Pacific", "Berlin"). 0 and ""
// without a clock.
void clockZone(int& offsetS, char* zone, const size_t cap) {
  offsetS = 0;
  zone[0] = '\0';
  struct tm local{};
  time_t utc = 0;
  if (!halClock.isAvailable() || !halClock.localTime(local) || !halClock.utcEpoch(utc)) return;
  offsetS = aitext::utcOffsetSeconds(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                                     local.tm_sec, static_cast<int64_t>(utc));
  const uint8_t index = timezones::activeIndex();
  if (index >= timezones::count()) return;
  const TimezoneInfo& entry = timezones::table()[index];
  const TzZone* mapped = entry.iana != nullptr ? tzGridZone(tzGridFindZone(entry.iana)) : nullptr;
  aitext::zoneWord(entry.name, mapped != nullptr ? mapped->display : nullptr, zone, cap);
}

// "/API Keys/gemini.txt" as the card spells the two names. 1 = found, 0 = not there, -1 = the card
// would not list its root.
int findKeyFile(char* path, const size_t cap) {
  HalFile root = Storage.open("/");
  if (!root || !root.isDirectory()) return -1;
  char name[128];
  for (HalFile dir = root.openNextFile(); dir; dir = root.openNextFile()) {
    if (!dir.isDirectory() || dir.getName(name, sizeof(name)) == 0 || !aitext::isKeyDirName(name)) continue;
    char folder[sizeof(name) + 1];
    std::snprintf(folder, sizeof(folder), "/%s", name);
    for (HalFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory() || file.getName(name, sizeof(name)) == 0 || !aitext::isKeyFileName(name)) continue;
      return std::snprintf(path, cap, "%s/%s", folder, name) < static_cast<int>(cap) ? 1 : 0;
    }
  }
  return 0;
}

const char* keyStateName(const KeyState s) {
  switch (s) {
    case KeyState::NotRead:
      return "not read";
    case KeyState::None:
      return "none";
    case KeyState::Ok:
      return "found";
    case KeyState::Refused:
      return "refused";
    case KeyState::Unreadable:
      return "unreadable";
  }
  return "?";
}

void quotaLoad(State* s) {
  if (s->quotaLoaded) return;
  s->quotaLoaded = true;
  HalFile file;
  if (!Storage.exists(QUOTA_FILE) || !Storage.openFileForRead("AI", QUOTA_FILE, file)) return;
  const int n = file.read(s->keyFile, QUOTA_READ_MAX);  // keyFile is free scratch outside reloadKey()
  if (n > 0 && geminiQuotaLoad(s->keyFile, static_cast<size_t>(n), &s->quotaTmp)) {
    geminiQuotaMerge(&s->quota, &s->quotaTmp);
  }
  std::memset(s->keyFile, 0, QUOTA_READ_MAX);
}

void quotaSave(State* s) {
  const size_t n = geminiQuotaSave(&s->quota, trustedUtc(), s->keyFile, QUOTA_READ_MAX);
  if (n == 0 || n >= QUOTA_READ_MAX) return;
  Storage.mkdir(QUOTA_DIR);
  HalFile file;
  if (!Storage.openFileForWrite("AI", QUOTA_FILE, file) || file.write(s->keyFile, n) != n) {
    LOG_ERR("AI", "The card refused %s", QUOTA_FILE);
  }
  std::memset(s->keyFile, 0, QUOTA_READ_MAX);
}

// A question's run: its hooks and where it stands.
struct Run {
  const Hooks* hooks;
  Progress progress;
  bool cancelled;
};

bool runTick(void* p) {
  auto* run = static_cast<Run*>(p);
  resetTaskWatchdogIfSubscribed();
  if (!run->cancelled && run->hooks->tick != nullptr && !run->hooks->tick(run->hooks->ctx)) run->cancelled = true;
  return !run->cancelled;
}

void setStage(Run& run, const char* stage, const char* model, const uint32_t waitUntilMs) {
  if (stage != nullptr) run.progress.stage = stage;
  if (model != nullptr) std::snprintf(run.progress.model, sizeof(run.progress.model), "%s", model);
  run.progress.waitUntilMs = waitUntilMs;
  if (run.hooks->changed != nullptr) run.hooks->changed(run.progress, run.hooks->ctx);
}

// A retry's wait, in slices that honour Cancel. False when cancelled.
bool waitSliced(Run& run, const uint32_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (!runTick(&run)) return false;
    delay(std::min<uint32_t>(AiHttps::SLICE_MS, ms - (millis() - t0) + 1));
  }
  return runTick(&run);
}

// The question cut to GEM_Q_MAX on a character boundary, on one line.
void copyQuestion(char* out, const char* in) {
  size_t n = std::strlen(in);
  if (n >= GEM_Q_MAX) {
    n = GEM_Q_MAX - 1;
    while (n > 0 && (static_cast<unsigned char>(in[n]) & 0xC0) == 0x80) n--;
  }
  for (size_t i = 0; i < n; i++) out[i] = (in[i] == '\n' || in[i] == '\r' || in[i] == '\t') ? ' ' : in[i];
  out[n] = '\0';
}

// THE WIKIPEDIA LOOKUP, once a question, before its first request - never per retry (wiki_lookup.h).
// ONE GET, never retried. Only the question's keywords leave the reader: no key, no question, no
// chat. Fills s->bg and s->src, or leaves both "" with out.lookupWhy saying why.
void lookup(State* s, Run& run, const GeminiTurnPair* ctx, const int nCtx, Outcome& out) {
  s->bg[0] = '\0';
  s->src[0] = '\0';
  if (!s->good.lookup) {
    std::snprintf(out.lookupWhy, sizeof(out.lookupWhy), "lookup=off in gemini.txt");
    return;
  }
  // A follow-up ("and when?") borrows from the topic's newest question that named something.
  const char* earlier[GEM_CONTEXT];
  for (int i = 0; i < nCtx && i < GEM_CONTEXT; i++) earlier[i] = ctx[i].q;
  if (wikiKeywords(s->question, nCtx > 0 ? earlier : nullptr, nCtx, s->kw, sizeof(s->kw)) == 0) {
    std::snprintf(out.lookupWhy, sizeof(out.lookupWhy), "no keywords in the question");
    return;
  }
  if (wikiUrl(s->kw, s->wikiUrl, sizeof(s->wikiUrl)) >= sizeof(s->wikiUrl)) {
    std::snprintf(out.lookupWhy, sizeof(out.lookupWhy), "the keywords did not fit the URL");
    return;
  }
  setStage(run, "Looking up Wikipedia...", nullptr, 0);
  AiHttps::Request rq;
  rq.url = s->wikiUrl;  // ⚠ never logged: the question's words are in it
  rq.host = WIKI_HOST;
  rq.roots = AiHttps::Roots::Wikipedia;
  rq.userAgent = WIKI_USER_AGENT;  // Wikimedia's policy: a name and a contact
  rq.dnsMs = WIKI_TOTAL_MS;
  rq.connectMs = WIKI_CONNECT_MS;
  rq.firstByteMs = WIKI_TOTAL_MS;
  rq.bodyIdleMs = WIKI_IDLE_MS;
  rq.totalMs = WIKI_TOTAL_MS;
  rq.sink = s->sink;
  rq.cap = WIKI_RESPONSE_CAP + 1;
  rq.tick = &runTick;
  rq.ctx = &run;
  AiHttps::Response rs;
  const int code = AiHttps::request(rq, rs);
  out.lookupAsked = true;
  out.lookupMs = rs.ms;
  if (rs.cancelled) {
    std::snprintf(out.lookupWhy, sizeof(out.lookupWhy), "cancelled");
    return;
  }
  if (code != 200) {
    wikiSkipWhy(code, rs.net, WIKI_BAD, out.lookupWhy, sizeof(out.lookupWhy));
    return;
  }
  const int parsed = wikiParse(s->sink, rs.length, &s->wiki);
  if (parsed != WIKI_OK) {
    wikiSkipWhy(200, GEM_NET_OK, parsed, out.lookupWhy, sizeof(out.lookupWhy));
    return;
  }
  // Only pages ABOUT the keywords, and the sources line naming exactly them - all or nothing.
  int why = WIKI_OK;
  const int used = wikiCompose(&s->wiki, s->kw, WIKI_BG_MAX, s->bg, sizeof(s->bg), s->src, sizeof(s->src), &why);
  if (used <= 0) {
    s->bg[0] = '\0';
    s->src[0] = '\0';
    wikiSkipWhy(200, GEM_NET_OK, why, out.lookupWhy, sizeof(out.lookupWhy));
    return;
  }
  out.lookupPages = used;
}

// The ladder for one question: a loop, never recursion.
void runLadder(State* s, Run& run, const GeminiTurnPair* ctx, const int nCtx, const int64_t askedUtc, Outcome& out) {
  const GeminiConfig& cfg = s->good;
  int tz = 0;
  char zone[24];
  clockZone(tz, zone, sizeof(zone));
  geminiModelLabel(cfg.model, s->primaryLabel, sizeof(s->primaryLabel));
  GeminiLadder& l = s->ladder;
  int step = geminiLadderStart(&l, &cfg, &s->session, &s->quota, askedUtc, millis());
  l.retryTimeouts = SETTINGS.aiRetryTimeouts != 0;  // the ladder's rule 6: the setting, as asked
  if (step == GEM_STEP_FAIL) {
    geminiFailText(0, GEM_NET_OK, nullptr, &l, cfg.key, tz, out.fail, sizeof(out.fail));
    return;
  }
  geminiModelLabel(l.model, s->label, sizeof(s->label));
  std::snprintf(run.progress.model, sizeof(run.progress.model), "%s", s->label);  // shown with the first stage
  lookup(s, run, ctx, nCtx, out);
  if (run.cancelled) {
    out.cancelled = true;
    return;
  }
  if (l.quotaSwitch) {
    geminiQuotaNotice(s->primaryLabel, s->label, l.primaryUntil, l.nowUtc, tz, l.primaryRemainS, s->stage,
                      sizeof(s->stage));
    setStage(run, s->stage, s->label, 0);
  } else {
    setStage(run, ASKING, s->label, 0);
  }
  for (;;) {
    if (run.cancelled) {
      out.cancelled = true;
      return;
    }
    // Today's date from the clock as it is when this request goes (a trusted clock only). The
    // background goes in the question's own turn, never in here.
    geminiSystemText(trustedUtc(), tz, zone, s->sys, sizeof(s->sys));
    const size_t n = geminiBuildRequest(ctx, nCtx, s->question, s->bg, s->sys, l.budget, s->body, BODY_CAP);
    if (n >= BODY_CAP) {
      out.net = GEM_NET_NOMEM;
      geminiFailText(0, GEM_NET_NOMEM, nullptr, nullptr, cfg.key, tz, out.fail, sizeof(out.fail));
      return;
    }
    std::snprintf(s->url, sizeof(s->url), "%s%s%s", GEM_URL_PREFIX, l.model, GEM_URL_SUFFIX);
    AiHttps::Request rq;
    rq.url = s->url;
    rq.host = GEM_HOST;
    rq.roots = AiHttps::Roots::Google;
    rq.body = s->body;
    rq.bodyLength = n;
    rq.apiKey = cfg.key;  // ⚠ a header, never the URL
    rq.userAgent = GEMINI_USER_AGENT;
    rq.dnsMs = GEMINI_DNS_MS;
    rq.connectMs = GEMINI_CONNECT_MS;
    rq.firstByteMs = GEMINI_FIRST_BYTE_MS;
    rq.bodyIdleMs = GEMINI_BODY_IDLE_MS;
    rq.sink = s->sink;
    rq.cap = SINK_CAP;
    rq.tick = &runTick;
    rq.ctx = &run;
    AiHttps::Response rs;
    const int code = AiHttps::request(rq, rs);
    out.attempts++;
    out.lastStatus = code;
    out.net = rs.net;
    run.progress.attempts = out.attempts;
    if (code <= 0 && rs.cancelled) {
      // Ended by Cancel: the user's word, never a timeout for rule 6 to retry.
      out.cancelled = true;
      return;
    }
    if (code > 0) {
      geminiParseReply(s->sink, rs.length, s->text, TEXT_CAP, &s->reply);
    } else {
      std::memset(&s->reply, 0, sizeof(s->reply));
      s->reply.retryDelayS = -1;
    }
    uint32_t delayMs = 0;
    const char* stage = nullptr;
    const bool wasFallback = l.onFallback;
    // A timeout or a lost connection is rule 6's: once more, then the fallback once - with the
    // background found above, never a second lookup.
    step =
        geminiLadderNext(&l, &s->session, &s->quota, code, rs.net, &s->reply, trustedUtc(), millis(), &delayMs, &stage);
    if (step == GEM_STEP_DONE) {
      if (s->reply.kind == GEM_REPLY_TEXT &&
          geminiAnswerText(&s->reply, s->text, s->bg[0] ? s->src : nullptr, s->answer, sizeof(s->answer)) > 0) {
        out.ok = true;
        // The scrub every text from Google gets: a model can echo what it was sent.
        geminiScrub(s->answer, sizeof(s->answer), cfg.key);
        geminiModelLabel(l.model, out.label, sizeof(out.label));
      } else {
        geminiNoTextMessage(&s->reply, out.fail, sizeof(out.fail));
      }
      return;
    }
    if (step == GEM_STEP_FAIL) {
      geminiFailText(code, rs.net, &s->reply, &l, cfg.key, tz, out.fail, sizeof(out.fail));
      return;
    }
    geminiModelLabel(l.model, s->label, sizeof(s->label));
    if (l.quotaSwitch && l.onFallback && !wasFallback) {
      geminiQuotaNotice(s->primaryLabel, s->label, l.primaryUntil, l.nowUtc, tz, l.primaryRemainS, s->stage,
                        sizeof(s->stage));
      stage = s->stage;
    }
    // A quota switch's notice (no wait) stays up through the fallback's request; a busy wait counts
    // down, then says it is asking again.
    setStage(run, stage != nullptr ? stage : ASKING, s->label, delayMs ? millis() + delayMs : 0);
    if (delayMs) {
      if (!waitSliced(run, delayMs)) {
        out.cancelled = true;
        return;
      }
      setStage(run, "Asking again...", nullptr, 0);
    }
  }
}

}  // namespace

void reloadKey() {
  State* s = state();
  char path[sizeof(key.path)];
  const int found = findKeyFile(path, sizeof(path));
  KeyState next = KeyState::None;
  int parsed = GEM_CFG_NO_KEY;
  if (found < 0) {
    next = KeyState::Unreadable;
  } else if (found > 0) {
    HalFile file;
    int n = -1;
    if (s != nullptr && Storage.openFileForRead("AI", path, file)) n = file.read(s->keyFile, KEY_READ_MAX);
    if (n < 0) {
      next = KeyState::Unreadable;
    } else {
      s->keyFile[n] = '\0';
      parsed = geminiParseConfig(s->keyFile, static_cast<size_t>(n), &s->parsed);
      std::memset(s->keyFile, 0, sizeof(s->keyFile));
      next = parsed == GEM_CFG_OK ? KeyState::Ok : (parsed == GEM_CFG_BAD_KEY ? KeyState::Refused : KeyState::None);
    }
  }
  if (s == nullptr) next = KeyState::Unreadable;

  key.state = next;
  std::snprintf(key.path, sizeof(key.path), "%s", found > 0 ? path : "");
  if (next == KeyState::Unreadable) {
    // The last good key stays (a card that fails one read is not "no key").
    key.usable = s != nullptr && s->haveGood;
  } else {
    if (next == KeyState::Ok) {
      s->good = s->parsed;
      s->haveGood = true;
    } else if (s != nullptr) {
      s->haveGood = false;
      std::memset(&s->good, 0, sizeof(s->good));
    }
    key.usable = next == KeyState::Ok;
    if (s != nullptr) {
      if (found == 0) geminiParseConfig(nullptr, 0, &s->parsed);  // the defaults, for the line
      geminiConfigLine(&s->parsed, parsed, key.line, sizeof(key.line));
    }
  }
  if (s != nullptr && key.usable) {
    key.length = s->good.keyLen;
    std::snprintf(key.model, sizeof(key.model), "%s", s->good.model);
    std::snprintf(key.fallback, sizeof(key.fallback), "%s", s->good.fallback);
    key.thinking = s->good.thinking;
    key.lookup = s->good.lookup;
  } else {
    key.length = 0;
    key.model[0] = '\0';
    key.fallback[0] = '\0';
    key.thinking = 0;
    key.lookup = true;
  }
  if (s != nullptr) std::memset(&s->parsed, 0, sizeof(s->parsed));  // ⚠ its key copy
  if (key.state != lastLogged) {
    lastLogged = key.state;
    LOG_INF("AI", "Key file: %s (%d chars)", keyStateName(key.state), key.length);
  }
}

const KeyInfo& keyInfo() { return key; }

const GeminiChat* chat() {
  State* s = state();
  return s != nullptr ? &s->chat : nullptr;
}

bool topicRunning() {
  State* s = state();
  if (s == nullptr || geminiChatTopicDue(&s->chat, trustedUtc())) return false;
  int idx[GEM_CONTEXT];
  return geminiChatContext(&s->chat, idx) > 0;
}

bool newTopic() {
  State* s = state();
  if (s == nullptr) return false;
  const bool running = topicRunning();
  geminiChatNewTopic(&s->chat);
  return running;
}

bool retryTimeouts() { return SETTINGS.aiRetryTimeouts != 0; }

void setRetryTimeouts(const bool on) {
  if ((SETTINGS.aiRetryTimeouts != 0) == on) return;
  SETTINGS.aiRetryTimeouts = on ? 1 : 0;
  SETTINGS.saveToFile();
  LOG_INF("AI", "Retry if it times out: %s", on ? "on" : "off");
}

size_t quotaNote(char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  State* s = state();
  if (s == nullptr) return 0;
  quotaLoad(s);
  const char* model = key.model[0] ? key.model : GEM_DEFAULT_MODEL;
  const int64_t now = trustedUtc();
  const uint32_t ms = millis();
  int64_t until = 0;
  uint32_t remain = 0;
  if (!geminiQuotaWhen(&s->quota, model, now, ms, &until, &remain)) return 0;
  int tz = 0;
  char zone[24];
  clockZone(tz, zone, sizeof(zone));
  if (key.fallback[0] && geminiQuotaOut(&s->quota, key.fallback, now, ms)) {
    geminiQuotaBack(&s->quota, model, key.fallback, now, ms, &until, &remain);
    return geminiQuotaAllOut(until, now, tz, remain, out, cap);
  }
  char a[GEM_LABEL_MAX];
  char b[GEM_LABEL_MAX];
  geminiModelLabel(model, a, sizeof(a));
  geminiModelLabel(key.fallback, b, sizeof(b));
  return geminiQuotaNotice(a, key.fallback[0] ? b : "", until, now, tz, remain, out, cap);
}

void ask(const char* question, const Hooks& hooks, Outcome& out) {
  out = Outcome{};
  const uint32_t t0 = millis();
  Run run{&hooks, Progress{}, false};
  run.progress.startMs = t0;
  run.progress.stage = ASKING;
  State* s = state();
  if (s == nullptr) {
    out.net = GEM_NET_NOMEM;
    geminiFailText(0, GEM_NET_NOMEM, nullptr, nullptr, nullptr, 0, out.fail, sizeof(out.fail));
    return;
  }
  copyQuestion(s->question, question != nullptr ? question : "");
  reloadKey();  // a new key works at once
  quotaLoad(s);
  std::memcpy(&s->quotaIn, &s->quota, sizeof(s->quota));
  const int64_t askedUtc = trustedUtc();
  const bool topic = geminiChatTopicDue(&s->chat, askedUtc);
  // The topic's earlier exchanges (none when this question starts one), oldest first. They point
  // into the chat, which does not change until this question is folded in at the end.
  GeminiTurnPair ctx[GEM_CONTEXT];
  int idx[GEM_CONTEXT];
  const int nCtx = topic ? 0 : geminiChatContext(&s->chat, idx);
  for (int i = 0; i < nCtx; i++) {
    ctx[i].q = s->chat.ex[idx[i]].q;
    ctx[i].a = s->chat.ex[idx[i]].a;
  }

  if (!key.usable) {
    std::snprintf(out.fail, sizeof(out.fail), "%s",
                  key.state == KeyState::Refused ? "The key in API Keys/gemini.txt was refused - see Key info"
                                                 : "No key - see Key info & help");
  } else {
    runLadder(s, run, ctx, nCtx, askedUtc, out);
  }
  std::memset(&s->reply, 0, sizeof(s->reply));  // Google's message may quote what was sent
  out.ms = millis() - t0;
  if (std::memcmp(&s->quotaIn, &s->quota, sizeof(s->quota)) != 0) quotaSave(s);

  const char* a = out.cancelled ? "Cancelled" : (out.ok ? s->answer : out.fail);
  geminiChatAdd(&s->chat, askedUtc, topic, !out.ok || out.cancelled, out.ok ? out.label : "", s->question, a);
  out.exchange = s->chat.n - 1;
  if (out.lookupWhy[0] && out.lookupAsked) LOG_INF("AI", "Wikipedia lookup skipped: %s", out.lookupWhy);
  // ⚠ Never the key, the question, its keywords or the answer: sizes, codes, the model, the time.
  LOG_INF("AI", "%s in %lu ms, %d request(s), HTTP %d, %s, question %u chars, answer %u chars, Wikipedia %d page(s)",
          out.cancelled ? "cancelled" : (out.ok ? "answered" : "failed"), static_cast<unsigned long>(out.ms),
          out.attempts, out.lastStatus, out.label[0] ? out.label : "-", static_cast<unsigned>(std::strlen(s->question)),
          static_cast<unsigned>(out.ok ? std::strlen(s->answer) : 0), out.lookupPages);
}

int addFailed(const char* question, const char* why) {
  State* s = state();
  if (s == nullptr) return -1;
  copyQuestion(s->question, question != nullptr ? question : "");
  const int64_t now = trustedUtc();
  geminiChatAdd(&s->chat, now, geminiChatTopicDue(&s->chat, now), true, "", s->question, why != nullptr ? why : "");
  return s->chat.n - 1;
}

}  // namespace AiRun
