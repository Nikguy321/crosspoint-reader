#pragma once

// Survival guide search. Pure.
//
// search.idx (built by scripts/guide/make_pack.py): one line a term, sorted by term (bytewise):
//   <term> TAB <topic-id>:<weight>,<topic-id>:<weight>,...
// A term is a stemmed lower-case word [a-z0-9]+ (stemWord; stopwords never indexed). A topic's
// weight for a term is title 3 + summary 2 + body 1 for the fields that hold it (1..6), or 1 when
// only a synonym (packs/guide/<id>/synonyms.txt) brought it in.
//
// A query is split into words like the builder splits text (lower-case [a-z0-9]+, at least 2
// characters, numbers at least 3, no stopwords), each word stemmed; each word PREFIX-matches the
// terms ("fir" finds fire, firewood); a topic must match every word (AND); its score is the sum
// over the words of its best weight among that word's matching terms. Results are ranked by score,
// then pack order. When that finds nothing, the topics whose title (then summary) holds the query
// text are returned instead (Catalog::titleSearch), with score 0.
//
// The index is used where it lies (no copy): the store loads the file (<= MAX_INDEX_BYTES) into
// PSRAM when the search screen opens and attaches it here.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "GdPack.h"

namespace gd {

constexpr int MAX_QUERY_WORDS = 8;
constexpr size_t MAX_TERM = 32;  // a longer query word is cut to this (still a prefix of its term)

// The light stemmer shared with make_pack.py (scripts/guide/stem_vectors.tsv holds its cases):
// plural -ies/-sses/-es/-s, then -ing (7+ letters) / -ed (6+), undoubling a final consonant. Only
// all-letter words change. word: lower-case [a-z0-9]. Writes the stem (NUL-terminated); its length.
size_t stemWord(std::string_view word, char* out, size_t cap);
bool isStopword(std::string_view word);
// The stopword list (the same as scripts/guide/stopwords.txt; the tests compare them).
int stopwordCount();
const char* stopword(int index);

struct QueryWords {
  char word[MAX_QUERY_WORDS][MAX_TERM + 1] = {};
  int count = 0;
};
// The query's searchable words, stemmed (duplicates dropped; at most MAX_QUERY_WORDS).
void splitQuery(std::string_view query, QueryWords& out);

struct Hit {
  uint16_t topic = 0;
  uint16_t score = 0;  // 0: a title/summary fallback hit
};

class SearchIndex {
 public:
  // Checks the whole text once (every line "<term>\t<id>:<w>,...", terms sorted and unique);
  // false (and detached) when it is not a valid index. The text must outlive the index.
  bool attach(const char* text, size_t len);
  void detach();
  bool attached() const { return text != nullptr; }
  int termCount() const { return terms; }

  // Ranked hits (at most cap). Postings naming a topic the catalog lacks are skipped.
  int query(std::string_view query, const Catalog& catalog, Hit* out, int cap);

  // The first line whose term is >= key (byte offset; len when none). For tests and the bench.
  size_t lowerBound(std::string_view key) const;

 private:
  // Calls fn(topicIndex, weight) for every posting of every term starting with prefix.
  template <typename Fn>
  void forPrefix(std::string_view prefix, const Catalog& catalog, Fn fn) const;

  const char* text = nullptr;
  size_t len = 0;
  int terms = 0;
  // Per-topic scratch for one query (~4 KB: keep the index in PSRAM with the store).
  uint16_t total[MAX_TOPICS] = {};
  uint8_t best[MAX_TOPICS] = {};
  uint8_t matched[MAX_TOPICS] = {};
};

}  // namespace gd
