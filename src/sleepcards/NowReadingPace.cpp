#include "NowReadingPace.h"

#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sleepcards::pace {
namespace {

float sessionSecondsPerPage(const PaceSession& s) {
  return s.samples >= MIN_SAMPLES && s.weight > 0 ? s.sum / s.weight : -1.0f;
}

// Parse one unsigned decimal field; advances p. False unless at least one digit was read.
bool nextField(const char*& p, const char* end, uint32_t& value) {
  while (p < end && *p == ' ') p++;
  if (p >= end || *p < '0' || *p > '9') return false;
  uint64_t v = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    v = v * 10 + static_cast<uint64_t>(*p - '0');
    if (v > UINT32_MAX) return false;
    p++;
  }
  value = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

float secondsPerPage(const PaceState& state) {
  return state.samples >= MIN_SAMPLES && state.weight > 0 ? state.weightedSum / state.weight : -1.0f;
}

bool observePage(PaceSession& s, const uint32_t nowMs, const int spineIndex, const int page, const int pageCount,
                 const float chapterShare) {
  if (spineIndex < 0 || page < 0) return false;
  if (pageCount > 0) {
    s.spineIndex = spineIndex;
    s.chapterPageCount = pageCount;
    s.fractionPerPage = chapterShare > 0 && chapterShare <= 1 ? chapterShare / static_cast<float>(pageCount) : 0;
  }
  if (s.havePrevious && s.previousSpine == spineIndex && s.previousPage == page) return false;  // a repaint

  bool sampled = false;
  // Only the next page of the same chapter: a chapter change includes building the new chapter,
  // and jumps or going back say nothing about reading speed.
  if (s.havePrevious && s.previousSpine == spineIndex && page == s.previousPage + 1) {
    const uint32_t gap = nowMs - s.previousMs;
    if (gap >= MIN_GAP_MS && gap <= MAX_GAP_MS) {
      float x = static_cast<float>(gap) / 1000.0f;
      float ref = sessionSecondsPerPage(s);
      if (ref <= 0) ref = s.referenceSpp;
      if (ref > 0) x = std::clamp(x, ref / OUTLIER_FACTOR, ref * OUTLIER_FACTOR);
      s.sum = (1 - ALPHA) * s.sum + ALPHA * x;
      s.weight = (1 - ALPHA) * s.weight + ALPHA;
      s.decay *= (1 - ALPHA);
      s.samples++;
      sampled = true;
    }
  }
  s.havePrevious = true;
  s.previousMs = nowMs;
  s.previousSpine = spineIndex;
  s.previousPage = page;
  return sampled;
}

void seedReference(PaceSession& session, const PaceState& stored) {
  const float spp = secondsPerPage(stored);
  if (spp > 0) session.referenceSpp = spp;
}

PaceState merged(const PaceState& stored, const PaceSession& s) {
  PaceState out = stored;
  if (s.samples > 0) {
    out.weightedSum = s.decay * stored.weightedSum + s.sum;
    out.weight = s.decay * stored.weight + s.weight;
    out.samples = stored.samples + s.samples;
  }
  if (s.spineIndex >= 0 && s.chapterPageCount > 0) {
    out.spineIndex = s.spineIndex;
    out.chapterPageCount = s.chapterPageCount;
    out.fractionPerPage = s.fractionPerPage;
  }
  return out;
}

void clearSamples(PaceSession& s) {
  const float spp = sessionSecondsPerPage(s);
  if (spp > 0) s.referenceSpp = spp;
  s.sum = 0;
  s.weight = 0;
  s.decay = 1;
  s.samples = 0;
}

size_t formatState(const PaceState& state, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  const float spp = state.weight > 0 ? state.weightedSum / state.weight : 0;
  const auto msPerPage = static_cast<unsigned long>(std::lround(std::clamp(spp, 0.0f, 3600.0f) * 1000.0f));
  const auto weightPpm = static_cast<unsigned long>(std::lround(std::clamp(state.weight, 0.0f, 1.0f) * 1e6f));
  const auto fppNano =
      static_cast<unsigned long>(std::llround(std::clamp(static_cast<double>(state.fractionPerPage), 0.0, 1.0) * 1e9));
  const int n = std::snprintf(out, cap, "NRP1 %lu %lu %lu %d %d %lu\n", msPerPage, weightPpm,
                              static_cast<unsigned long>(state.samples), std::max(state.spineIndex, -1) + 1,
                              std::max(state.chapterPageCount, 0), fppNano);
  if (n < 0) return 0;
  return std::min(static_cast<size_t>(n), cap - 1);
}

bool parseState(const char* text, const size_t len, PaceState& out) {
  if (text == nullptr || len < 5 || std::strncmp(text, "NRP1 ", 5) != 0) return false;
  const char* p = text + 4;
  const char* end = text + len;
  uint32_t v[6];
  for (auto& field : v) {
    if (!nextField(p, end, field)) return false;
  }
  if (p < end && *p != '\n' && *p != '\r' && *p != '\0') return false;
  if (v[1] > 1000000 || v[0] > 3600000 || v[5] > 1000000000) return false;
  PaceState s;
  s.weight = static_cast<float>(v[1]) / 1e6f;
  s.weightedSum = static_cast<float>(v[0]) / 1000.0f * s.weight;
  s.samples = v[2];
  s.spineIndex = static_cast<int>(std::min<uint32_t>(v[3], 65536)) - 1;
  s.chapterPageCount = static_cast<int>(std::min<uint32_t>(v[4], 65535));
  s.fractionPerPage = static_cast<float>(static_cast<double>(v[5]) / 1e9);
  out = s;
  return true;
}

TimeLeft estimateTimeLeft(const PaceState& pace, const CardBook& book) {
  TimeLeft t;
  const float spp = secondsPerPage(pace);
  if (spp <= 0) return t;
  if (book.chapterPageCount > 0 && book.chapterPage >= 0 && book.chapterPage < book.chapterPageCount) {
    const int pagesLeft = book.chapterPageCount - book.chapterPage;
    t.chapterMinutes = std::max(1, static_cast<int>(std::lround(pagesLeft * spp / 60.0f)));
  }
  const bool sameChapter = pace.fractionPerPage > 0 && pace.spineIndex >= 0 && pace.spineIndex == book.spineIndex &&
                           pace.chapterPageCount == book.chapterPageCount;
  if (sameChapter && book.bookFraction >= 0 && book.bookFraction < 1) {
    const double pages = (1.0 - book.bookFraction) / pace.fractionPerPage;
    const double minutes = std::min(pages * spp / 60.0, 999.0 * 60.0);
    t.bookMinutes = std::max({1, static_cast<int>(std::lround(minutes)), t.chapterMinutes});
  }
  return t;
}

bool formatMinutes(int minutes, char* out, const size_t cap) {
  if (out == nullptr || cap == 0 || minutes < 0) return false;
  minutes = std::max(minutes, 1);
  if (minutes < 60) {
    std::snprintf(out, cap, tr(STR_NR_MINUTES), minutes);
  } else if (minutes < 10 * 60) {
    const int rounded = (minutes + 2) / 5 * 5;
    const int h = rounded / 60;
    const int m = rounded % 60;
    if (m == 0) {
      std::snprintf(out, cap, tr(STR_NR_HOURS), h);
    } else {
      std::snprintf(out, cap, tr(STR_NR_HOURS_MINUTES), h, m);
    }
  } else {
    std::snprintf(out, cap, tr(STR_NR_HOURS), (minutes + 30) / 60);
  }
  return true;
}

PaceSession& session() {
  static PaceSession s;
  return s;
}

}  // namespace sleepcards::pace
