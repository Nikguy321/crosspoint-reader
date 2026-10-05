// Survival guide saves on the host: state.txt round trip, refusal of every truncation and of
// hand-made corruptions, sanitizing against a pack that changed; bookmarks (toggle, cap, prune, bad
// lines skipped); recent (touch, cap, prune); the .tmp fallback and the paths.
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <random>
#include <string>

#include "GuideTestUtil.h"

using namespace gd;
using guidetest::Buf;

namespace {

const char* const CATS = "emergency\tE\tx\t1\nfire\tF\ty\t2\n";
const char* const TOPICS =
    "call-for-help\temergency\tCall\t-\tt/call-for-help.gp\t2\ts\n"
    "fire-lays\tfire\tLays\t-\tt/fire-lays.gp\t3\ts\n"
    "bow-drill\tfire\tBow\t-\tt/bow-drill.gp\t1\ts\n";

struct Pack {
  Buf cats{CATS}, topics{TOPICS};
  std::unique_ptr<Catalog> c = std::make_unique<Catalog>();
  Pack() { EXPECT_EQ(c->parse(cats.data(), cats.len, topics.data(), topics.len), PackError::None); }
};

State sample() {
  State s;
  s.screen = Screen::Page;
  s.list = ListKind::Search;
  std::strcpy(s.category, "fire");
  std::strcpy(s.query, "bow drill");
  std::strcpy(s.topic, "fire-lays");
  s.page = 2;
  s.sub = SUB_LAST;
  s.sel = 5;
  return s;
}

std::string format(const State& s) {
  char buf[STATE_TEXT_MAX];
  const size_t n = formatState(s, buf, sizeof(buf));
  EXPECT_GT(n, 0u);
  return std::string(buf, n);
}

bool same(const State& a, const State& b) {
  return a.screen == b.screen && a.list == b.list && std::strcmp(a.category, b.category) == 0 &&
         std::strcmp(a.query, b.query) == 0 && std::strcmp(a.topic, b.topic) == 0 && a.page == b.page &&
         a.sub == b.sub && a.sel == b.sel;
}

}  // namespace

TEST(GuideState, RoundTrip) {
  const State s = sample();
  const std::string text = format(s);
  EXPECT_EQ(text,
            "GS1\nscreen page\nlist search\ncat fire\nquery bow drill\ntopic fire-lays\npage 2\nsub 255\nsel 5\nend\n");
  State back;
  ASSERT_TRUE(parseState(text.data(), text.size(), back));
  EXPECT_TRUE(same(s, back));
  // Defaults round-trip too ("-" ids, an empty query).
  const State d;
  const std::string dt = format(d);
  EXPECT_NE(dt.find("query\n"), std::string::npos);
  EXPECT_NE(dt.find("cat -\n"), std::string::npos);
  ASSERT_TRUE(parseState(dt.data(), dt.size(), back));
  EXPECT_TRUE(same(d, back));
  // CRLF is read.
  std::string crlf;
  for (const char c : text) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
  ASSERT_TRUE(parseState(crlf.data(), crlf.size(), back));
  EXPECT_TRUE(same(s, back));
  // Too small a buffer: 0.
  char tiny[20];
  EXPECT_EQ(formatState(s, tiny, sizeof(tiny)), 0u);
}

TEST(GuideState, QueryIsOneSafeLine) {
  State s;
  std::strcpy(s.query, "two\nlines\there");
  State back;
  const std::string text = format(s);
  ASSERT_TRUE(parseState(text.data(), text.size(), back));
  EXPECT_STREQ(back.query, "two lines here");
}

TEST(GuideState, EveryTruncationIsRefused) {
  const std::string text = format(sample());
  State back;
  for (size_t n = 0; n + 1 < text.size(); n++) {
    EXPECT_FALSE(parseState(text.data(), n, back)) << n;
  }
  EXPECT_TRUE(parseState(text.data(), text.size() - 1, back));  // only the final '\n' missing
}

TEST(GuideState, CorruptionsAreRefusedOrValid) {
  const std::string good = format(sample());
  const char* const bad[] = {
      "GS2\nscreen page\nlist search\ncat fire\nquery q\ntopic t\npage 0\nsub 0\nsel 0\nend\n",
      "GS1\nscreen book\nlist search\ncat fire\nquery q\ntopic t\npage 0\nsub 0\nsel 0\nend\n",
      "GS1\nscreen page\nlist books\ncat fire\nquery q\ntopic t\npage 0\nsub 0\nsel 0\nend\n",
      "GS1\nscreen page\nlist search\ncat Fire\nquery q\ntopic t\npage 0\nsub 0\nsel 0\nend\n",
      "GS1\nscreen page\nlist search\ncat fire\nquery q\ntopic t\npage 64\nsub 0\nsel 0\nend\n",
      "GS1\nscreen page\nlist search\ncat fire\nquery q\ntopic t\npage 0\nsub 256\nsel 0\nend\n",
      "GS1\nscreen page\nlist search\ncat fire\nquery q\ntopic t\npage 0\nsub 0\nsel 1024\nend\n",
      "GS1\nscreen page\nlist search\ncat fire\nquery q\ntopic t\npage 0\nsub 0\nsel 0\n",
      "GS1\nscreen page\nlist search\ncat fire\nquery q\ntopic t\npage 0\nsub 0\nsel 0\nend\nmore\n",
      "GS1\nlist search\nscreen page\ncat fire\nquery q\ntopic t\npage 0\nsub 0\nsel 0\nend\n",
  };
  State back;
  for (const char* b : bad) EXPECT_FALSE(parseState(b, std::strlen(b), back)) << b;
  std::string longQuery = "GS1\nscreen home\nlist none\ncat -\nquery " + std::string(MAX_QUERY + 1, 'q') +
                          "\ntopic -\npage 0\nsub 0\nsel 0\nend\n";
  EXPECT_FALSE(parseState(longQuery.data(), longQuery.size(), back));
  // Random byte mutations: refused, or a state that formats back to itself.
  std::mt19937 rng(11);
  for (int i = 0; i < 3000; i++) {
    std::string m = good;
    m[rng() % m.size()] = static_cast<char>(rng() % 256);
    if (parseState(m.data(), m.size(), back)) {
      State again;
      const std::string re = format(back);
      ASSERT_TRUE(parseState(re.data(), re.size(), again));
      EXPECT_TRUE(same(back, again));
    }
  }
}

TEST(GuideState, SanitizeFitsThePack) {
  Pack p;
  State s = sample();
  EXPECT_FALSE(sanitizeState(s, *p.c));  // all known; page 2 < 3 pages
  std::strcpy(s.topic, "bow-drill");     // 1 page
  EXPECT_TRUE(sanitizeState(s, *p.c));
  EXPECT_EQ(s.page, 0);
  EXPECT_EQ(s.sub, 0);
  std::strcpy(s.topic, "gone");
  EXPECT_TRUE(sanitizeState(s, *p.c));
  EXPECT_STREQ(s.topic, "");
  EXPECT_EQ(s.screen, Screen::List);  // back to its list (the search)
  s.list = ListKind::Category;
  std::strcpy(s.category, "nope");
  EXPECT_TRUE(sanitizeState(s, *p.c));
  EXPECT_EQ(s.list, ListKind::None);
  EXPECT_EQ(s.screen, Screen::Home);
  State page;
  page.screen = Screen::Page;  // a page with no topic and no list: home
  EXPECT_TRUE(sanitizeState(page, *p.c));
  EXPECT_EQ(page.screen, Screen::Home);
  State search;
  search.screen = Screen::List;
  search.list = ListKind::Search;  // an empty query: no list
  EXPECT_TRUE(sanitizeState(search, *p.c));
  EXPECT_EQ(search.screen, Screen::Home);
  State about;
  about.screen = Screen::About;
  EXPECT_FALSE(sanitizeState(about, *p.c));
}

TEST(GuideMarks, ToggleCapAndRoundTrip) {
  auto m = std::make_unique<Marks>();
  EXPECT_TRUE(m->toggle("fire-lays", 1));
  EXPECT_TRUE(m->has("fire-lays", 1));
  EXPECT_FALSE(m->has("fire-lays", 0));
  EXPECT_TRUE(m->toggle("bow-drill", 0));
  EXPECT_STREQ(m->items[0].topic, "bow-drill");  // newest first
  EXPECT_FALSE(m->toggle("fire-lays", 1));       // removed
  EXPECT_EQ(m->count, 1);
  EXPECT_FALSE(m->toggle("Bad Id", 0));
  EXPECT_FALSE(m->toggle("x", MAX_PAGES));
  EXPECT_EQ(m->count, 1);
  for (int i = 0; i < MAX_MARKS + 5; i++) m->toggle("t" + std::to_string(i), 0);
  EXPECT_EQ(m->count, MAX_MARKS);
  EXPECT_STREQ(m->items[0].topic, ("t" + std::to_string(MAX_MARKS + 4)).c_str());
  EXPECT_FALSE(m->has("bow-drill", 0));  // the oldest dropped
  char buf[MARKS_TEXT_MAX];
  const size_t n = formatMarks(*m, buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  auto back = std::make_unique<Marks>();
  ASSERT_TRUE(parseMarks(buf, n, *back));
  ASSERT_EQ(back->count, m->count);
  for (int i = 0; i < m->count; i++) {
    EXPECT_STREQ(back->items[i].topic, m->items[i].topic);
    EXPECT_EQ(back->items[i].page, m->items[i].page);
  }
  // A full list with the longest ids fits MARKS_TEXT_MAX.
  for (int i = 0; i < MAX_MARKS; i++) {
    std::string id(MAX_ID, 'a');
    id[0] = static_cast<char>('a' + i % 26);
    id[1] = static_cast<char>('a' + i / 26);
    std::strcpy(m->items[i].topic, id.c_str());
    m->items[i].page = MAX_PAGES - 1;
  }
  m->count = MAX_MARKS;
  EXPECT_GT(formatMarks(*m, buf, sizeof(buf)), 0u);
}

TEST(GuideMarks, BadLinesAreSkippedAndPruneFitsThePack) {
  const char* text =
      "GM1\nfire-lays 2\nnot a mark\nBAD 1\nbow-drill 0\nfire-lays 2\ngone 1\nbow-drill 64\ncall-for-help 9\n"
      "end\nafter-end 1\n";
  auto m = std::make_unique<Marks>();
  ASSERT_TRUE(parseMarks(text, std::strlen(text), *m));
  ASSERT_EQ(m->count, 4);  // fire-lays 2, bow-drill 0, gone 1, call-for-help 9 (the duplicate dropped)
  Pack p;
  EXPECT_TRUE(m->prune(*p.c));
  ASSERT_EQ(m->count, 3);
  EXPECT_STREQ(m->items[2].topic, "call-for-help");
  EXPECT_EQ(m->items[2].page, 1);  // clamped to its 2 pages
  EXPECT_FALSE(m->prune(*p.c));
  // Clamping can make two marks the same: one is kept.
  auto d = std::make_unique<Marks>();
  const char* dup = "GM1\nbow-drill 3\nbow-drill 0\nend\n";
  ASSERT_TRUE(parseMarks(dup, std::strlen(dup), *d));
  EXPECT_TRUE(d->prune(*p.c));
  EXPECT_EQ(d->count, 1);
  EXPECT_FALSE(parseMarks("GMX\n", 4, *d));
  EXPECT_EQ(d->count, 0);
  EXPECT_TRUE(parseMarks("GM1", 3, *d));  // a header alone: no marks
}

TEST(GuideRecent, TouchCapPruneRoundTrip) {
  auto r = std::make_unique<Recent>();
  r->touch("a");
  r->touch("b");
  r->touch("a");
  ASSERT_EQ(r->count, 2);
  EXPECT_STREQ(r->items[0], "a");
  EXPECT_STREQ(r->items[1], "b");
  r->touch("Bad!");
  EXPECT_EQ(r->count, 2);
  for (int i = 0; i < MAX_RECENT + 3; i++) r->touch("t" + std::to_string(i));
  EXPECT_EQ(r->count, MAX_RECENT);
  EXPECT_STREQ(r->items[0], ("t" + std::to_string(MAX_RECENT + 2)).c_str());
  r->touch("t5");  // already there: moves to the front, nothing drops
  EXPECT_EQ(r->count, MAX_RECENT);
  EXPECT_STREQ(r->items[0], "t5");
  char buf[RECENT_TEXT_MAX];
  const size_t n = formatRecent(*r, buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  auto back = std::make_unique<Recent>();
  ASSERT_TRUE(parseRecent(buf, n, *back));
  ASSERT_EQ(back->count, r->count);
  for (int i = 0; i < r->count; i++) EXPECT_STREQ(back->items[i], r->items[i]);
  const char* text = "GR1\nfire-lays\nBAD\ngone\nfire-lays\nbow-drill\nend\n";
  ASSERT_TRUE(parseRecent(text, std::strlen(text), *back));
  EXPECT_EQ(back->count, 3);
  Pack p;
  EXPECT_TRUE(back->prune(*p.c));
  ASSERT_EQ(back->count, 2);
  EXPECT_STREQ(back->items[0], "fire-lays");
  EXPECT_STREQ(back->items[1], "bow-drill");
  EXPECT_FALSE(parseRecent("", 0, *back));
}

TEST(GuideSaves, TmpFallbackAndPaths) {
  EXPECT_EQ(loadFrom(true, true), LoadFrom::Main);
  EXPECT_EQ(loadFrom(true, false), LoadFrom::Main);
  EXPECT_EQ(loadFrom(false, true), LoadFrom::Tmp);
  EXPECT_EQ(loadFrom(false, false), LoadFrom::None);
  char buf[80];
  ASSERT_GT(statePath("survival", STATE_FILE, buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "/.crosspoint/guide/survival/state.txt");
  ASSERT_GT(stateDir("survival", buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "/.crosspoint/guide/survival");
  char tmp[80];
  ASSERT_GT(tmpPathOf("/.crosspoint/guide/survival/marks.txt", tmp, sizeof(tmp)), 0u);
  EXPECT_STREQ(tmp, "/.crosspoint/guide/survival/marks.txt.tmp");
  EXPECT_EQ(tmpPathOf("/.crosspoint/guide/survival/marks.txt", tmp, 10), 0u);
}
