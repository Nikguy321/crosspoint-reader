// Apps > AI's own pure helpers (src/activities/apps/AiText). The answer page is the survival guide's
// page format, so every page built here is parsed by lib/Guide's own parser and laid out over the
// guide tests' fake font: a page that does not parse would show the app's "damaged" message instead
// of an answer, and one past PageLayout's screens would lose its end.
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GuideTestUtil.h"
#include "activities/apps/AiText.h"
#include "gemini.h"

namespace {

std::string page(const char* question, const char* note, const char* body) {
  const size_t n = aitext::buildPage(question, note, body, nullptr, 0);
  std::string out(n + 1, '\0');
  EXPECT_EQ(aitext::buildPage(question, note, body, out.data(), out.size()), n);
  out.resize(n);
  return out;
}

// Parses `text` as one guide page; the parsed blocks as "<kind>:<text>" lines.
std::string blocks(const std::string& text, std::string* title = nullptr) {
  guidetest::Buf buf(text);
  auto t = std::make_unique<gd::TopicText>();
  const gd::GpError err = t->parse(buf.data(), buf.len);
  EXPECT_EQ(err, gd::GpError::None) << "line " << t->errorLine() << " of:\n" << text;
  if (err != gd::GpError::None) return "";
  EXPECT_EQ(t->pageCount(), 1) << text;
  if (title) *title = t->page(0).title;
  std::string out;
  for (int i = 0; i < t->blockCount(); i++) {
    const gd::Block& b = t->block(i);
    static const char* const KIND[] = {"P", "B", "S", "W", "N"};
    out += KIND[static_cast<int>(b.kind)];
    if (b.kind == gd::BlockKind::Step) out += std::to_string(b.number);
    out += ":";
    out += b.text;
    out += "\n";
  }
  return out;
}

}  // namespace

TEST(AiText, AnswerPage) {
  std::string title;
  const std::string text = page("Has Tomb Raider: Legacy of Atlantis come out?", nullptr,
                                "Not yet.\n\nIt is due in 2027.\n- PC\n- consoles\n1. first\n2. second\n\n"
                                "Wikipedia: Tomb Raider: Legacy of Atlantis");
  EXPECT_EQ(blocks(text, &title),
            "P:Not yet.\nP:It is due in 2027.\nB:PC\nB:consoles\nS1:first\nS2:second\n"
            "P:Wikipedia: Tomb Raider: Legacy of Atlantis\n");
  EXPECT_EQ(title, "Has Tomb Raider: Legacy of Atlantis come out?");
}

TEST(AiText, MarkupIsEscaped) {
  // Lines the page format would read as a title, a figure, a box, a page break or an escape.
  const std::string text =
      page("q", nullptr,
           "= not a title\n@fig not-a-figure caption\n! not a warning\n* not a note\n---\n"
           "\\backslash\n0. zero is no step\n100. nor three digits\n1000. nor four\n-dash\n*star*\n"
           "99. but two are");
  EXPECT_EQ(blocks(text),
            "P:= not a title\nP:@fig not-a-figure caption\nP:! not a warning\nP:* not a note\nP:---\n"
            "P:\\backslash\nP:0. zero is no step\nP:100. nor three digits\nP:1000. nor four\nP:-dash\nP:*star*\n"
            "S99:but two are\n");
}

TEST(AiText, EmptyItemsAndBlanksAreLeftOut) {
  const std::string text = page("  spaced   question \t", nullptr, "\n   \n- \n-  \n3. \n  indented line  \n\r\n");
  std::string title;
  EXPECT_EQ(blocks(text, &title), "P:indented line\n");
  EXPECT_EQ(title, "spaced question");
  EXPECT_EQ(blocks(page("", nullptr, ""), &title), "");
  EXPECT_EQ(title, "Question");
  EXPECT_EQ(blocks(page(" \t ", nullptr, nullptr), &title), "");
  EXPECT_EQ(title, "Question");
}

TEST(AiText, LongQuestionIsCutAndShownWhole) {
  std::string q;
  while (q.size() < 300) q += "how many words fit in a title line ";
  std::string title;
  const std::string got = blocks(page(q.c_str(), nullptr, "Answer."), &title);
  EXPECT_LE(title.size(), aitext::TITLE_MAX);
  EXPECT_EQ(title.substr(title.size() - 3), "...");
  const std::string kept = title.substr(0, title.size() - 3);
  EXPECT_EQ(q.compare(0, kept.size(), kept), 0);
  EXPECT_EQ(q[kept.size()], ' ') << "cut at a word: " << title;
  std::string whole = q;
  while (!whole.empty() && whole.back() == ' ') whole.pop_back();
  EXPECT_EQ(got, "P:" + whole + "\nP:Answer.\n");
  // A 98-character question is cut too (the title holds 97 and "...").
  std::string q98(98, 'x');
  EXPECT_EQ(blocks(page(q98.c_str(), nullptr, "a"), &title), "P:" + q98 + "\nP:a\n");
  EXPECT_EQ(title, std::string(97, 'x') + "...");
  // Never inside a UTF-8 character when there is no space to cut at.
  std::string accents;
  while (accents.size() < 150) accents += "\xC3\xA9";
  blocks(page(accents.c_str(), nullptr, "a"), &title);
  EXPECT_EQ(title.size() % 2, 1u) << "97 bytes would split a character: 96 + \"...\"";
}

TEST(AiText, FailedQuestionNote) {
  EXPECT_EQ(blocks(page("q", "Gemini did not answer in time", "")), "N:Gemini did not answer in time\n");
  EXPECT_EQ(blocks(page("q", "  ", "x")), "P:x\n");
  EXPECT_EQ(blocks(page("q", "two\nlines", "")), "N:two lines\n");
}

TEST(AiText, LengthRule) {
  const std::string whole = page("question", nullptr, "body line");
  char small[8];
  EXPECT_EQ(aitext::buildPage("question", nullptr, "body line", small, sizeof(small)), whole.size());
  EXPECT_STREQ(small, "");
}

// The longest answer the app keeps (GEM_A_MAX) - every line a paragraph, or a run of bullets -
// lays out in PageLayout's screens with nothing cut, over the guide tests' font (10 px a byte, 20 px
// lines: narrower lines than the device's NotoSans 12, so more screens than the device needs).
TEST(AiText, LongestAnswerFitsTheLayout) {
  std::string body;
  int n = 0;
  while (body.size() < GEM_A_MAX - 120) {
    body += (n % 5 == 4) ? "\n" : "";
    body += (n % 3 == 0) ? "- " : "";
    body += "The quick brown fox jumps over the lazy dog, again and again. ";
    if (n % 2) body += "\n";
    n++;
  }
  body.resize(GEM_A_MAX - 100);
  body += "\n\nWikipedia: Fox; Dog";
  std::string q(GEM_Q_CAP, 'q');
  for (size_t i = 7; i < q.size(); i += 8) q[i] = ' ';
  const std::string text = page(q.c_str(), "a note", body.c_str());
  guidetest::Buf buf(text);
  auto t = std::make_unique<gd::TopicText>();
  ASSERT_EQ(t->parse(buf.data(), buf.len), gd::GpError::None);
  guidetest::FakeMetrics metrics;
  auto layout = std::make_unique<gd::PageLayout>();
  const int screens = gd::layoutPage(*t, 0, "Flash-Lite", metrics, gd::Geometry{}, nullptr, layout.get());
  EXPECT_GE(screens, 2);
  EXPECT_LE(screens, gd::PageLayout::MAX_SCREENS);
  EXPECT_FALSE(layout->truncated()) << screens << " screens";
}

TEST(AiText, KeyFolder) {
  EXPECT_TRUE(aitext::isKeyDirName("API Keys"));
  EXPECT_TRUE(aitext::isKeyDirName("api keys"));
  EXPECT_FALSE(aitext::isKeyDirName("API Keys2"));
  EXPECT_FALSE(aitext::isKeyDirName(nullptr));
  EXPECT_TRUE(aitext::isKeyFileName("Gemini.TXT"));
  EXPECT_FALSE(aitext::isKeyFileName("gemini.txt.bak"));
  EXPECT_TRUE(aitext::isKeyFolderPath("/API Keys/gemini.txt"));
  EXPECT_TRUE(aitext::isKeyFolderPath("/api keys/Other.txt"));
  EXPECT_TRUE(aitext::isKeyFolderPath("/API Keys"));
  EXPECT_TRUE(aitext::isKeyFolderPath("//API Keys/x/y.epub"));
  EXPECT_TRUE(aitext::isKeyFolderPath("API Keys/gemini.txt"));
  EXPECT_FALSE(aitext::isKeyFolderPath("/Books/API Keys/gemini.txt"));
  EXPECT_FALSE(aitext::isKeyFolderPath("/API Keys2/gemini.txt"));
  EXPECT_FALSE(aitext::isKeyFolderPath("/"));
  EXPECT_FALSE(aitext::isKeyFolderPath(nullptr));
  // Each name as the card reads it: SdFat skips leading spaces and drops trailing dots and spaces,
  // so these all open /API Keys - and a name with nothing left (".", " ") is read past.
  EXPECT_TRUE(aitext::isKeyFolderPath("/API Keys./gemini.txt"));
  EXPECT_TRUE(aitext::isKeyFolderPath("/ API Keys /gemini.txt"));
  EXPECT_TRUE(aitext::isKeyFolderPath("/api keys. . "));
  EXPECT_TRUE(aitext::isKeyFolderPath("/./API Keys/gemini.txt"));
  EXPECT_TRUE(aitext::isKeyFolderPath("/ /API Keys"));
  EXPECT_FALSE(aitext::isKeyFolderPath("/API.Keys/gemini.txt"));
  EXPECT_FALSE(aitext::isKeyFolderPath("/API  Keys/gemini.txt"));
  EXPECT_FALSE(aitext::isKeyFolderPath("/.API Keys/gemini.txt"));

  // The network servers' rule: the name anywhere in the path.
  EXPECT_TRUE(aitext::pathHasKeyDir("/API Keys/gemini.txt"));
  EXPECT_TRUE(aitext::pathHasKeyDir("/API Keys"));
  EXPECT_TRUE(aitext::pathHasKeyDir("/Books/api KEYS/x.epub"));
  EXPECT_TRUE(aitext::pathHasKeyDir("/Books/API Keys."));
  EXPECT_TRUE(aitext::pathHasKeyDir("API Keys"));
  EXPECT_FALSE(aitext::pathHasKeyDir("/Books/API Keys2/x.epub"));
  EXPECT_FALSE(aitext::pathHasKeyDir("/Books/my API Keys/x.epub"));
  EXPECT_FALSE(aitext::pathHasKeyDir("/Books/gemini.txt"));
  EXPECT_FALSE(aitext::pathHasKeyDir("/"));
  EXPECT_FALSE(aitext::pathHasKeyDir(""));
  EXPECT_FALSE(aitext::pathHasKeyDir(nullptr));
}

TEST(AiText, ZoneWord) {
  char out[24];
  aitext::zoneWord("Los Angeles / Vancouver", "Pacific", out, sizeof(out));
  EXPECT_STREQ(out, "Pacific");
  aitext::zoneWord("Phoenix", "Mountain, no DST", out, sizeof(out));
  EXPECT_STREQ(out, "Mountain, no DST");
  aitext::zoneWord("Berlin / Paris / Madrid / Rome", nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "Berlin");
  aitext::zoneWord("Berlin / Paris", "", out, sizeof(out));
  EXPECT_STREQ(out, "Berlin");
  aitext::zoneWord("UTC", nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "UTC");
  aitext::zoneWord(nullptr, nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "");
  char tiny[5];
  aitext::zoneWord("Honolulu", nullptr, tiny, sizeof(tiny));
  EXPECT_STREQ(tiny, "Hono");
  // What the date sentence makes of each (gemini.cpp keeps letters and spaces, cuts at a comma, and
  // says "local" for an offset's name).
  char sys[GEM_SYS_MAX];
  geminiSystemText(1791752700, -7 * 3600, "Mountain, no DST", sys, sizeof(sys));
  EXPECT_EQ(std::string(sys).find("Today is Sunday 11 October 2026, about 2:05 PM Mountain time (UTC-7). "), 0u);
  geminiSystemText(1791752700, 2 * 3600, "Berlin", sys, sizeof(sys));
  EXPECT_EQ(std::string(sys).find("Today is Sunday 11 October 2026, about 11:05 PM Berlin time (UTC+2). "), 0u);
  geminiSystemText(1791752700, 0, "UTC", sys, sizeof(sys));
  EXPECT_EQ(std::string(sys).find("Today is Sunday 11 October 2026, about 9:05 PM local time (UTC). "), 0u);
}

TEST(AiText, UtcOffset) {
  // 2026-10-11 21:05:00 UTC.
  const int64_t utc = 1791752700;
  EXPECT_EQ(aitext::utcOffsetSeconds(2026, 10, 11, 14, 5, 0, utc), -7 * 3600);        // Pacific daylight
  EXPECT_EQ(aitext::utcOffsetSeconds(2026, 10, 12, 2, 35, 0, utc), 5 * 3600 + 1800);  // India, the next day
  EXPECT_EQ(aitext::utcOffsetSeconds(2026, 10, 11, 21, 5, 0, utc), 0);
  EXPECT_EQ(aitext::utcOffsetSeconds(2026, 10, 11, 21, 5, 29, utc), 0) << "seconds apart: the same minute";
  EXPECT_EQ(aitext::utcOffsetSeconds(2026, 10, 11, 14, 4, 59, utc), -7 * 3600) << "rounded to the minute";
  EXPECT_EQ(aitext::utcOffsetSeconds(2026, 10, 11, 7, 5, 0, utc), -14 * 3600);
  EXPECT_EQ(aitext::utcOffsetSeconds(2027, 1, 1, 0, 0, 0, 1798761600), 0) << "2027-01-01 00:00 UTC";
}
