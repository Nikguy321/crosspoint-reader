#include "GdSearch.h"

#include <cstring>

#include "GdText.h"

namespace gd {

namespace {

// scripts/guide/stopwords.txt, in the same (sorted) order.
constexpr const char* STOPWORDS[] = {
    "a",      "about", "after", "all",   "also",  "an",    "and",  "any",  "are",  "as",   "at",   "be",    "been",
    "before", "but",   "by",    "can",   "do",    "does",  "each", "for",  "from", "get",  "has",  "have",  "if",
    "in",     "into",  "is",    "it",    "its",   "may",   "more", "most", "much", "not",  "of",   "off",   "on",
    "one",    "only",  "or",    "other", "out",   "over",  "so",   "some", "than", "that", "the",  "their", "them",
    "then",   "there", "these", "they",  "this",  "to",    "too",  "up",   "use",  "very", "was",  "way",   "we",
    "were",   "what",  "when",  "where", "which", "while", "who",  "will", "with", "you",  "your",
};
constexpr int STOPWORD_COUNT = sizeof(STOPWORDS) / sizeof(STOPWORDS[0]);

bool endsWith(const std::string_view s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool allLetters(const std::string_view s) {
  for (const char c : s) {
    if (c < 'a' || c > 'z') return false;
  }
  return !s.empty();
}

// Drops one letter of a final doubled consonant (not a vowel, l, s or z): "cutt" -> "cut".
size_t undouble(const char* w, size_t n) {
  if (n >= 2 && w[n - 1] == w[n - 2] && !std::strchr("aeioulsz", w[n - 1])) return n - 1;
  return n;
}

// The term of the line starting at offset `at` (up to its TAB, or the line's end).
std::string_view termAt(const char* text, const size_t len, const size_t at) {
  size_t e = at;
  while (e < len && text[e] != '\t' && text[e] != '\n') e++;
  return std::string_view(text + at, e - at);
}

size_t lineEnd(const char* text, const size_t len, size_t at) {
  while (at < len && text[at] != '\n') at++;
  return at;
}

}  // namespace

size_t stemWord(const std::string_view word, char* out, const size_t cap) {
  if (cap == 0) return 0;
  char w[MAX_TERM + 8];
  size_t n = word.size() < MAX_TERM ? word.size() : MAX_TERM;
  std::memcpy(w, word.data(), n);
  if (allLetters(std::string_view(w, n))) {
    std::string_view s(w, n);
    if (endsWith(s, "ies") && n >= 5) {
      n -= 3;
      w[n++] = 'y';
    } else if (endsWith(s, "sses")) {
      n -= 2;
    } else if (endsWith(s, "es") && n >= 5 &&
               (std::strchr("sxz", w[n - 3]) || (w[n - 4] == 'c' && w[n - 3] == 'h') ||
                (w[n - 4] == 's' && w[n - 3] == 'h'))) {
      n -= 2;
    } else if (endsWith(s, "s") && n >= 4 && !endsWith(s, "ss") && !endsWith(s, "us") && !endsWith(s, "is")) {
      n -= 1;
    }
    s = std::string_view(w, n);
    if (endsWith(s, "ing") && n >= 7) {
      n = undouble(w, n - 3);
    } else if (endsWith(s, "ed") && n >= 6) {
      n = undouble(w, n - 2);
    }
  }
  return copyCut(std::string_view(w, n), out, cap);
}

bool isStopword(const std::string_view word) {
  int lo = 0, hi = STOPWORD_COUNT;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    const int c = std::string_view(STOPWORDS[mid]).compare(word);
    if (c == 0) return true;
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return false;
}

int stopwordCount() { return STOPWORD_COUNT; }
const char* stopword(const int index) { return STOPWORDS[index]; }

void splitQuery(const std::string_view query, QueryWords& out) {
  out.count = 0;
  char word[MAX_TERM + 1];
  size_t i = 0;
  while (i < query.size() && out.count < MAX_QUERY_WORDS) {
    while (i < query.size() && !isAlnumLower(lowerAscii(query[i]))) i++;
    size_t n = 0;
    bool digits = true;
    size_t full = 0;
    while (i < query.size() && isAlnumLower(lowerAscii(query[i]))) {
      const char c = lowerAscii(query[i++]);
      if (c < '0' || c > '9') digits = false;
      if (n < MAX_TERM) word[n++] = c;
      full++;
    }
    if (full < 2 || (digits && full < 3)) continue;
    const std::string_view w(word, n);
    if (isStopword(w)) continue;
    char stem[MAX_TERM + 1];
    stemWord(w, stem, sizeof(stem));
    bool dup = false;
    for (int k = 0; k < out.count; k++) dup = dup || std::strcmp(out.word[k], stem) == 0;
    if (!dup) copyCut(stem, out.word[out.count++], sizeof(out.word[0]));
  }
}

void SearchIndex::detach() {
  text = nullptr;
  len = 0;
  terms = 0;
}

bool SearchIndex::attach(const char* t, const size_t n) {
  detach();
  if (!t || n > MAX_INDEX_BYTES) return false;
  std::string_view prev;
  int count = 0;
  size_t at = 0;
  while (at < n) {
    const size_t end = lineEnd(t, n, at);
    const std::string_view line(t + at, end - at);
    const size_t tab = line.find('\t');
    if (tab == std::string_view::npos || tab == 0) return false;
    const std::string_view term = line.substr(0, tab);
    for (const char c : term) {
      if (!isAlnumLower(c)) return false;
    }
    if (count > 0 && term.compare(prev) <= 0) return false;  // sorted, unique
    // Postings: id:weight[,id:weight]...
    std::string_view rest = line.substr(tab + 1);
    if (rest.empty()) return false;
    while (!rest.empty()) {
      const size_t comma = rest.find(',');
      const std::string_view posting = rest.substr(0, comma);
      const size_t colon = posting.find(':');
      uint32_t w = 0;
      if (colon == std::string_view::npos || !validId(posting.substr(0, colon), MAX_ID) ||
          !parseUint(posting.substr(colon + 1), 255, w) || w == 0) {
        return false;
      }
      if (comma == std::string_view::npos) break;
      rest = rest.substr(comma + 1);
      if (rest.empty()) return false;  // a trailing comma
    }
    prev = term;
    count++;
    at = end + 1;
  }
  text = t;
  len = n;
  terms = count;
  return true;
}

size_t SearchIndex::lowerBound(const std::string_view key) const {
  if (!text) return 0;
  // lo and hi are line starts; every line starting before lo is < key, every line at or after hi
  // is >= key.
  size_t lo = 0, hi = len;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    size_t ls = mid;
    while (ls > lo && text[ls - 1] != '\n') ls--;
    if (termAt(text, len, ls).compare(key) < 0) {
      lo = lineEnd(text, len, ls) + 1;
      if (lo > hi) lo = hi;
    } else {
      hi = ls;
    }
  }
  return lo;
}

template <typename Fn>
void SearchIndex::forPrefix(const std::string_view prefix, const Catalog& catalog, Fn fn) const {
  size_t at = lowerBound(prefix);
  while (at < len) {
    const std::string_view term = termAt(text, len, at);
    if (term.size() < prefix.size() || term.compare(0, prefix.size(), prefix) != 0) break;
    const size_t end = lineEnd(text, len, at);
    std::string_view rest(text + at + term.size() + 1, end - at - term.size() - 1);
    while (!rest.empty()) {
      const size_t comma = rest.find(',');
      const std::string_view posting = rest.substr(0, comma);
      const size_t colon = posting.find(':');
      uint32_t w = 0;
      if (colon != std::string_view::npos && parseUint(posting.substr(colon + 1), 255, w)) {
        const int t = catalog.findTopic(posting.substr(0, colon));
        if (t >= 0) fn(t, static_cast<int>(w));
      }
      if (comma == std::string_view::npos) break;
      rest = rest.substr(comma + 1);
    }
    at = end + 1;
  }
}

int SearchIndex::query(const std::string_view q, const Catalog& catalog, Hit* out, const int cap) {
  if (cap <= 0) return 0;
  int n = 0;
  QueryWords words;
  splitQuery(q, words);
  const int topicCount = catalog.topicCount();
  if (text && words.count > 0) {
    std::memset(total, 0, sizeof(uint16_t) * topicCount);
    std::memset(matched, 0, static_cast<size_t>(topicCount));
    for (int k = 0; k < words.count; k++) {
      std::memset(best, 0, static_cast<size_t>(topicCount));
      forPrefix(words.word[k], catalog, [&](const int t, const int w) {
        if (w > best[t]) best[t] = static_cast<uint8_t>(w);
      });
      for (int t = 0; t < topicCount; t++) {
        if (best[t]) {
          total[t] = static_cast<uint16_t>(total[t] + best[t]);
          matched[t]++;
        }
      }
    }
    for (int t = 0; t < topicCount; t++) {
      if (matched[t] != words.count) continue;
      // Insert, ranked by score (higher first), then pack order; keep the best cap.
      int pos = n;
      while (pos > 0 && out[pos - 1].score < total[t]) pos--;
      if (pos >= cap) continue;
      const int last = n < cap ? n : cap - 1;
      for (int i = last; i > pos; i--) out[i] = out[i - 1];
      out[pos] = Hit{static_cast<uint16_t>(t), total[t]};
      if (n < cap) n++;
    }
  }
  if (n == 0) {
    uint16_t found[64];
    const int m = catalog.titleSearch(q, found, cap < 64 ? cap : 64);
    for (int i = 0; i < m; i++) out[n++] = Hit{found[i], 0};
  }
  return n;
}

}  // namespace gd
