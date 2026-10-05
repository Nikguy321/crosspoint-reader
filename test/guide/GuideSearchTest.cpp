// Survival guide search on the host: the stemmer against the shared vectors
// (scripts/guide/stem_vectors.tsv, which make_pack.py also checks), the stopwords against
// scripts/guide/stopwords.txt, query splitting, index validation, lowerBound over the raw text, and
// ranking: AND across words, prefix matching, the best weight a word, score then pack order, and the
// title / summary fallback.
#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "GuideTestUtil.h"

using namespace gd;
using guidetest::Buf;

namespace {

std::string stem(const std::string& w) {
  char out[MAX_TERM + 1];
  stemWord(w, out, sizeof(out));
  return out;
}

const char* const CATS = "a\tA\tx\t1\nb\tB\ty\t2\n";
const char* const TOPICS =
    "fire-lays\ta\tFire lays\t-\tt/fire-lays.gp\t1\tWays to stack wood\n"
    "bow-drill\ta\tBow drill\t-\tt/bow-drill.gp\t1\tFriction fire\n"
    "bleeding\tb\tBleeding and shock\tM\tt/bleeding.gp\t1\tPress hard, tourniquet\n"
    "water\tb\tSafe water\t-\tt/water.gp\t1\tBoil or treat it\n";
// Sorted by term; weights as the builder gives them.
const char* const INDEX =
    "bleed\tbleeding:5\n"
    "blood\tbleeding:1\n"
    "boil\twater:3\n"
    "bow\tbow-drill:4\n"
    "drill\tbow-drill:3\n"
    "fire\tfire-lays:4,bow-drill:3,water:1\n"
    "firewood\tfire-lays:1\n"
    "friction\tbow-drill:2\n"
    "ghost\tnot-a-topic:3\n"
    "lay\tfire-lays:3\n"
    "tie\tbow-drill:2,fire-lays:2\n"
    "tourniquet\tbleeding:2\n"
    "water\twater:4,fire-lays:1\n"
    "wood\tfire-lays:2,bow-drill:1\n";

struct Fixture {
  Buf cats{CATS}, topics{TOPICS};
  std::unique_ptr<Catalog> c = std::make_unique<Catalog>();
  std::unique_ptr<SearchIndex> idx = std::make_unique<SearchIndex>();
  std::string text = INDEX;
  Fixture() {
    EXPECT_EQ(c->parse(cats.data(), cats.len, topics.data(), topics.len), PackError::None);
    EXPECT_TRUE(idx->attach(text.data(), text.size()));
  }
  std::vector<std::pair<std::string, int>> q(const std::string& query) {
    Hit hits[16];
    const int n = idx->query(query, *c, hits, 16);
    std::vector<std::pair<std::string, int>> out;
    for (int i = 0; i < n; i++) out.emplace_back(c->topic(hits[i].topic).id, hits[i].score);
    return out;
  }
};

using Hits = std::vector<std::pair<std::string, int>>;

}  // namespace

TEST(GuideSearch, StemmerMatchesTheSharedVectors) {
  const std::string text = guidetest::readFile(std::string(GUIDE_SCRIPTS) + "/stem_vectors.tsv");
  ASSERT_FALSE(text.empty());
  std::istringstream in(text);
  std::string line;
  int checked = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t tab = line.find('\t');
    ASSERT_NE(tab, std::string::npos) << line;
    EXPECT_EQ(stem(line.substr(0, tab)), line.substr(tab + 1)) << line;
    checked++;
  }
  EXPECT_GE(checked, 30);
  EXPECT_EQ(stem(std::string(40, 'a')).size(), MAX_TERM);  // cut
}

TEST(GuideSearch, StopwordsMatchTheBuildersList) {
  const std::string text = guidetest::readFile(std::string(GUIDE_SCRIPTS) + "/stopwords.txt");
  ASSERT_FALSE(text.empty());
  std::istringstream in(text);
  std::string line;
  std::vector<std::string> file;
  while (std::getline(in, line)) {
    if (!line.empty() && line[0] != '#') file.push_back(line);
  }
  ASSERT_EQ(static_cast<int>(file.size()), stopwordCount());
  for (int i = 0; i < stopwordCount(); i++) {
    EXPECT_EQ(file[i], stopword(i));
    if (i > 0) EXPECT_LT(std::string(stopword(i - 1)), std::string(stopword(i)));  // sorted (binary search)
    EXPECT_TRUE(isStopword(stopword(i)));
  }
  EXPECT_FALSE(isStopword("fire"));
  EXPECT_FALSE(isStopword(""));
}

TEST(GuideSearch, SplitQuery) {
  QueryWords w;
  splitQuery("  How to make a FIRE, fires & 911!  x 30 ", w);
  ASSERT_EQ(w.count, 4);
  EXPECT_STREQ(w.word[0], "how");
  EXPECT_STREQ(w.word[1], "make");
  EXPECT_STREQ(w.word[2], "fire");  // "fires" stems to it too: one word
  EXPECT_STREQ(w.word[3], "911");   // "x" (one letter) and "30" (a short number) dropped
  splitQuery("the and of", w);
  EXPECT_EQ(w.count, 0);
  splitQuery("a b c d e f g h i j k l m n o p q r s t u v w x y z aa bb cc dd ee ff gg hh ii", w);
  EXPECT_EQ(w.count, MAX_QUERY_WORDS);
  splitQuery("caf\xC3\xA9 bleeding", w);  // a non-ASCII letter splits the word
  ASSERT_EQ(w.count, 2);
  EXPECT_STREQ(w.word[0], "caf");
  EXPECT_STREQ(w.word[1], "bleed");
}

TEST(GuideSearch, AttachValidatesTheIndex) {
  auto idx = std::make_unique<SearchIndex>();
  const auto ok = [&](const std::string& s) { return idx->attach(s.data(), s.size()); };
  EXPECT_TRUE(ok(""));
  EXPECT_EQ(idx->termCount(), 0);
  EXPECT_TRUE(ok("a\tt:1\nb\tt:2,u:6"));  // no final newline
  EXPECT_EQ(idx->termCount(), 2);
  EXPECT_FALSE(ok("b\tt:1\na\tt:1\n"));    // unsorted
  EXPECT_FALSE(ok("a\tt:1\na\tt:1\n"));    // duplicate
  EXPECT_FALSE(ok("A\tt:1\n"));            // upper case
  EXPECT_FALSE(ok("a\n"));                 // no postings
  EXPECT_FALSE(ok("a\t\n"));               // empty postings
  EXPECT_FALSE(ok("\tt:1\n"));             // empty term
  EXPECT_FALSE(ok("a\tt:0\n"));            // weight 0
  EXPECT_FALSE(ok("a\tt:256\n"));          // weight > 255
  EXPECT_FALSE(ok("a\tt\n"));              // no weight
  EXPECT_FALSE(ok("a\tT:1\n"));            // bad id
  EXPECT_FALSE(ok("a\tt:1,\n"));           // trailing comma
  EXPECT_FALSE(ok("a\tt:1\n\nb\tt:1\n"));  // blank line
  EXPECT_FALSE(ok("a\tt:1\r\n"));          // CR
  EXPECT_FALSE(idx->attached());
  const std::string big(MAX_INDEX_BYTES + 1, 'a');
  EXPECT_FALSE(ok(big));
}

TEST(GuideSearch, LowerBound) {
  Fixture f;
  const std::string& t = f.text;
  EXPECT_EQ(f.idx->lowerBound(""), 0u);
  EXPECT_EQ(t.compare(f.idx->lowerBound("bleed"), 6, "bleed\t"), 0);
  EXPECT_EQ(t.compare(f.idx->lowerBound("c"), 6, "drill\t"), 0);
  EXPECT_EQ(t.compare(f.idx->lowerBound("fire"), 5, "fire\t"), 0);
  EXPECT_EQ(t.compare(f.idx->lowerBound("firf"), 9, "friction\t"), 0);
  EXPECT_EQ(f.idx->lowerBound("zzz"), t.size());
  EXPECT_EQ(f.idx->lowerBound("a"), 0u);
  EXPECT_EQ(f.idx->termCount(), 14);
}

TEST(GuideSearch, RanksByScoreThenPackOrder) {
  Fixture f;
  // "fire" prefix-matches fire and firewood: each topic's best weight counts once.
  EXPECT_EQ(f.q("fire"), (Hits{{"fire-lays", 4}, {"bow-drill", 3}, {"water", 1}}));
  // AND: both words must match; scores add.
  EXPECT_EQ(f.q("fire wood"), (Hits{{"fire-lays", 6}, {"bow-drill", 4}}));
  EXPECT_EQ(f.q("bow fire"), (Hits{{"bow-drill", 7}}));
  // Prefixes and stems: "drilling" -> "drill"; "boil" via "boi".
  EXPECT_EQ(f.q("drilling"), (Hits{{"bow-drill", 3}}));
  EXPECT_EQ(f.q("boi"), (Hits{{"water", 3}}));
  // Synonym postings (weight 1) and stopwords dropped.
  EXPECT_EQ(f.q("the blood"), (Hits{{"bleeding", 1}}));
  // Postings for topics the catalog lacks are skipped.
  EXPECT_TRUE(f.q("ghost").empty());
  EXPECT_EQ(f.q("water wood"), (Hits{{"fire-lays", 3}}));
  // Ties keep pack order (fire-lays is listed second in the posting).
  EXPECT_EQ(f.q("tie"), (Hits{{"fire-lays", 2}, {"bow-drill", 2}}));
}

TEST(GuideSearch, FallsBackToTitlesThenSummaries) {
  Fixture f;
  // No term matches "shock" (not indexed here): the title holds it.
  EXPECT_EQ(f.q("shock"), (Hits{{"bleeding", 0}}));
  // Only stopwords / short words: nothing to search, the title fallback still runs.
  EXPECT_EQ(f.q("and"), (Hits{{"bleeding", 0}}));
  EXPECT_EQ(f.q("stack"), (Hits{{"fire-lays", 0}}));  // a summary word
  EXPECT_TRUE(f.q("qqq").empty());
  EXPECT_TRUE(f.q("").empty());
  // Detached: only the fallback.
  f.idx->detach();
  EXPECT_EQ(f.q("bow"), (Hits{{"bow-drill", 0}}));
}

TEST(GuideSearch, CapKeepsTheBest) {
  Fixture f;
  Hit hits[2];
  ASSERT_EQ(f.idx->query("fire", *f.c, hits, 2), 2);
  EXPECT_STREQ(f.c->topic(hits[0].topic).id, "fire-lays");
  EXPECT_STREQ(f.c->topic(hits[1].topic).id, "bow-drill");
  ASSERT_EQ(f.idx->query("fire", *f.c, hits, 1), 1);
  EXPECT_STREQ(f.c->topic(hits[0].topic).id, "fire-lays");
  EXPECT_EQ(f.idx->query("fire", *f.c, hits, 0), 0);
}
