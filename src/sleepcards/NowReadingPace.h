#pragma once

#include <cstddef>
#include <cstdint>

#include "SleepCard.h"

// The owner's reading pace, for the Now Reading card's "time left" line.
//
// The EPUB reader reports every page it shows (noteReaderPage); a step to the
// next page of the same chapter 1 s .. 5 min after the last one is a sample of
// seconds per page. Samples feed a bias-corrected exponential moving average
// (weighted sum / weight, so the first sample is already the estimate). What
// the reader measured since boot is a PaceSession: the same EMA applied on top
// of whatever the card last stored, so session and stored state merge exactly
// as if the samples had been applied one by one (merged()). The stored state is
// one short text line in STATE_PATH, written by the reader every FLUSH_EVERY
// samples, on every sleep, and by the card when it shows.
namespace sleepcards::pace {

constexpr const char* STATE_DIR = "/.crosspoint/sleepcards";
constexpr const char* STATE_PATH = "/.crosspoint/sleepcards/now_reading.dat";
constexpr size_t STATE_TEXT_CAP = 96;

constexpr uint32_t MIN_GAP_MS = 1000;           // quicker = skimming or skipping
constexpr uint32_t MAX_GAP_MS = 5 * 60 * 1000;  // longer = a pause, not reading
constexpr float ALPHA = 0.1f;                   // weight of the newest sample
constexpr float OUTLIER_FACTOR = 3.0f;          // a sample is clamped to [ref / 3, ref * 3]
constexpr uint32_t MIN_SAMPLES = 5;             // fewer: no estimate yet
constexpr uint32_t FLUSH_EVERY = 16;            // reader persists after this many new samples

// The persisted pace plus the layout of the chapter last read (for the book estimate).
struct PaceState {
  float weightedSum = 0;  // EMA numerator, seconds
  float weight = 0;       // EMA denominator 0..1
  uint32_t samples = 0;
  int spineIndex = -1;        // chapter (spine item) the layout below belongs to; -1 none
  int chapterPageCount = 0;   // its page count at the time
  float fractionPerPage = 0;  // share of the whole book one of its pages is; 0 unknown
};

// Measured since boot and not yet persisted.
struct PaceSession {
  // EMA delta: stored' = decay * stored + (sum, weight).
  float sum = 0;
  float weight = 0;
  float decay = 1;
  uint32_t samples = 0;
  float referenceSpp = 0;  // seconds per page to judge outliers by (the stored estimate); 0 none
  // The page shown last.
  bool havePrevious = false;
  uint32_t previousMs = 0;
  int previousSpine = -1;
  int previousPage = -1;
  // The chapter layout seen last.
  int spineIndex = -1;
  int chapterPageCount = 0;
  float fractionPerPage = 0;
};

// Seconds per page, or a negative value while there are fewer than MIN_SAMPLES.
float secondsPerPage(const PaceState& state);

// A page became visible at nowMs. chapterShare = the chapter's share of the book (0..1; 0 unknown).
// Returns true when this produced a sample. The same page again (a repaint) changes nothing.
bool observePage(PaceSession& session, uint32_t nowMs, int spineIndex, int page, int pageCount, float chapterShare);
// Use a stored estimate as the outlier reference until the session has its own.
void seedReference(PaceSession& session, const PaceState& stored);

// stored with the session's samples applied on top; the session's layout replaces the stored one.
PaceState merged(const PaceState& stored, const PaceSession& session);
// After merged() was persisted: forget the samples, keep the page chain and layout.
void clearSamples(PaceSession& session);

// One line of text, "NRP1 <msPerPage> <weightPpm> <samples> <spine> <pages> <fppNano>\n". Returns the length.
size_t formatState(const PaceState& state, char* out, size_t cap);
// Parse formatState()'s line. False (out untouched) on anything else.
bool parseState(const char* text, size_t len, PaceState& out);

// The estimate for the open book, in minutes; -1 = unknown.
struct TimeLeft {
  int chapterMinutes = -1;
  int bookMinutes = -1;
};
// The chapter needs the pace and the chapter's page position; the book also needs the layout of
// the same chapter (spine index and page count match), so a stale layout of another book is never
// used.
TimeLeft estimateTimeLeft(const PaceState& pace, const CardBook& book);

// Minutes as the card says them: "~12 min", "~5 h 40 min" (5-minute steps from 1 h), "~12 h"
// (hours from 10 h). Returns false for a negative value.
bool formatMinutes(int minutes, char* out, size_t cap);

// The reader's pace since boot (one per device).
PaceSession& session();

// Device only (DeviceNowReadingPace.cpp): the EPUB reader's hook, called once per page shown.
// Records the sample and persists every FLUSH_EVERY samples; seeds the reference once per boot.
void noteReaderPage(uint32_t nowMs, int spineIndex, int page, int pageCount, float chapterShare);
// Device only: save the session's unsaved samples (one small write; nothing when there are none).
// SleepActivity calls it on every sleep - deep sleep reboots and would lose them.
void flushReaderPace();

}  // namespace sleepcards::pace
