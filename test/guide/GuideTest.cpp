// Survival guide on the host, the parsers and navigation: pack.txt (format refusal, required keys,
// cut values), categories.tsv / topics.tsv (order, ids, flags, files, caps), the .gp page format
// (every block kind, escapes, figures, every error), the PNG size reader, and NEXT / PREV across
// screens, pages and topics with the n/m counter.
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include "GuideTestUtil.h"

using namespace gd;
using guidetest::Buf;

namespace {

const char* const PACK_TXT =
    "# a comment\n"
    "format=1\n"
    "id=survival\n"
    "version=2026.10.1\n"
    "title=Survival Guide\n"
    "short=SURVIVAL\n"
    "status=reviewed-by-ai\n"
    "status_text=Reviewed by AI.\n"
    "note=Reference only.\n"
    "ref_note=Reference only - call for help first.\n"
    "license=MIT\n"
    "min_app=1\n"
    "future_key=whatever\n";

PackError packInfo(const std::string& text, PackInfo& out) { return parsePackInfo(text.data(), text.size(), out); }

const char* const CATS =
    "emergency\tEMERGENCY\tDo these first\t1\n"
    "fire\tFIRE\tMake fire\t2\n"
    "water\tWATER\tFind water\t3\n";
const char* const TOPICS =
    "call-for-help\temergency\tCall for help\t-\tt/call-for-help.gp\t2\tCall or text 911\n"
    "signal-now\temergency\tSignal for rescue now\tQ\tt/signal-now.gp\t1\tThrees mean help\n"
    "fire-basics\tfire\tFire basics\t-\tt/fire-basics.gp\t3\tThe fire triangle\n"
    "fire-lays\tfire\tFire lays\t-\tt/fire-lays.gp\t2\tWays to stack wood\n"
    "bleeding\twater\tBleeding: stop it\tQM\tt/bleeding.gp\t1\tPress hard\n";

struct Cat {
  Buf cats, topics;
  std::unique_ptr<Catalog> c = std::make_unique<Catalog>();
  PackError err;
  Cat(const std::string& cs, const std::string& ts) : cats(cs), topics(ts) {
    err = c->parse(cats.data(), cats.len, topics.data(), topics.len);
  }
};

}  // namespace

// ---- pack.txt ------------------------------------------------------------------------------------

TEST(GuidePackInfo, ParsesEveryKey) {
  PackInfo info;
  ASSERT_EQ(packInfo(PACK_TXT, info), PackError::None);
  EXPECT_EQ(info.format, 1);
  EXPECT_EQ(info.minApp, 1);
  EXPECT_STREQ(info.id, "survival");
  EXPECT_STREQ(info.version, "2026.10.1");
  EXPECT_STREQ(info.title, "Survival Guide");
  EXPECT_STREQ(info.shortTitle, "SURVIVAL");
  EXPECT_STREQ(info.status, "reviewed-by-ai");
  EXPECT_STREQ(info.statusText, "Reviewed by AI.");
  EXPECT_STREQ(info.note, "Reference only.");
  EXPECT_STREQ(info.refNote, "Reference only - call for help first.");
  EXPECT_STREQ(info.license, "MIT");
}

TEST(GuidePackInfo, NewerFormatOrAppNeedsNewerFirmware) {
  PackInfo info;
  EXPECT_EQ(packInfo("format=2\nid=survival\nversion=1\ntitle=T\n", info), PackError::NeedsNewerFirmware);
  // Checked before anything else: the rest of a newer pack may not parse here.
  EXPECT_EQ(packInfo("format=7\nthis line is not key value\n", info), PackError::NeedsNewerFirmware);
  EXPECT_EQ(packInfo("garbage=\x01\nformat=3\n", info), PackError::NeedsNewerFirmware);
  EXPECT_EQ(packInfo("format=1\nmin_app=2\nid=survival\nversion=1\ntitle=T\n", info), PackError::NeedsNewerFirmware);
  EXPECT_EQ(packInfo("format=1\nmin_app=1\nid=survival\nversion=1\ntitle=T\n", info), PackError::None);
}

TEST(GuidePackInfo, MissingOrBadKeysAreDamaged) {
  PackInfo info;
  EXPECT_EQ(packInfo("", info), PackError::Damaged);
  EXPECT_EQ(packInfo("id=survival\nversion=1\ntitle=T\n", info), PackError::Damaged);            // no format
  EXPECT_EQ(packInfo("format=0\nid=survival\nversion=1\ntitle=T\n", info), PackError::Damaged);  // format 0
  EXPECT_EQ(packInfo("format=x\nid=survival\nversion=1\ntitle=T\n", info), PackError::Damaged);
  EXPECT_EQ(packInfo("format=1\nversion=1\ntitle=T\n", info), PackError::Damaged);               // no id
  EXPECT_EQ(packInfo("format=1\nid=Survival\nversion=1\ntitle=T\n", info), PackError::Damaged);  // bad id
  EXPECT_EQ(packInfo("format=1\nid=survival\ntitle=T\n", info), PackError::Damaged);             // no version
  EXPECT_EQ(packInfo("format=1\nid=survival\nversion=1\ntitle=\n", info), PackError::Damaged);   // empty title
  EXPECT_EQ(packInfo("format=1\nid=survival\nversion=1\ntitle=T\nnoequals\n", info), PackError::Damaged);
  EXPECT_EQ(packInfo("format=1\nid=survival\nversion=1\ntitle=T\nmin_app=x\n", info), PackError::Damaged);
  EXPECT_EQ(packInfo(std::string(MAX_PACKTXT_BYTES + 1, '#'), info), PackError::Damaged);
  EXPECT_EQ(packInfo("format=1\r\nid=survival\r\nversion=1\r\ntitle=T\r\n", info), PackError::None);  // CRLF
}

TEST(GuidePackInfo, LongValuesAreCutAndShortDefaultsToTitle) {
  PackInfo info;
  const std::string longTitle(200, 'x');
  ASSERT_EQ(packInfo("format=1\nid=survival\nversion=1\ntitle=" + longTitle + "\n", info), PackError::None);
  EXPECT_EQ(std::strlen(info.title), sizeof(info.title) - 1);
  EXPECT_EQ(std::strlen(info.shortTitle), sizeof(info.shortTitle) - 1);
  // A multi-byte character is never split.
  std::string t(46, 'a');
  t += "\xE2\x80\x94";  // em dash: bytes 47..49 of a 48-byte field
  ASSERT_EQ(packInfo("format=1\nid=survival\nversion=1\ntitle=" + t + "\n", info), PackError::None);
  EXPECT_EQ(std::strlen(info.title), 46u);
}

// ---- categories.tsv / topics.tsv -------------------------------------------------------------------

TEST(GuideCatalog, ParsesAndFinds) {
  Cat k(CATS, TOPICS);
  ASSERT_EQ(k.err, PackError::None);
  const Catalog& c = *k.c;
  ASSERT_EQ(c.categoryCount(), 3);
  ASSERT_EQ(c.topicCount(), 5);
  EXPECT_STREQ(c.category(0).title, "EMERGENCY");
  EXPECT_STREQ(c.category(1).blurb, "Make fire");
  EXPECT_EQ(c.category(0).first, 0);
  EXPECT_EQ(c.category(0).count, 2);
  EXPECT_EQ(c.category(1).first, 2);
  EXPECT_EQ(c.category(1).count, 2);
  EXPECT_EQ(c.category(2).first, 4);
  EXPECT_EQ(c.category(2).count, 1);
  const Topic& t = c.topic(4);
  EXPECT_STREQ(t.id, "bleeding");
  EXPECT_STREQ(t.title, "Bleeding: stop it");
  EXPECT_STREQ(t.file, "t/bleeding.gp");
  EXPECT_STREQ(t.summary, "Press hard");
  EXPECT_TRUE(t.quick());
  EXPECT_TRUE(t.medical());
  EXPECT_EQ(t.pages, 1);
  EXPECT_EQ(t.category, 2);
  EXPECT_FALSE(c.topic(0).quick());
  EXPECT_FALSE(c.topic(0).medical());
  EXPECT_EQ(c.findTopic("fire-lays"), 3);
  EXPECT_EQ(c.findTopic("call-for-help"), 0);
  EXPECT_EQ(c.findTopic("nope"), -1);
  EXPECT_EQ(c.findTopic(""), -1);
  EXPECT_EQ(c.findCategory("water"), 2);
  EXPECT_EQ(c.findCategory("food"), -1);
  uint16_t q[8];
  ASSERT_EQ(c.quickTopics(q, 8), 2);
  EXPECT_EQ(q[0], 1);
  EXPECT_EQ(q[1], 4);
  EXPECT_EQ(c.quickCount(), 2);
  EXPECT_EQ(c.quickTopics(q, 1), 1);
}

TEST(GuideCatalog, TitleSearchTitlesFirstThenSummaries) {
  Cat k(CATS, TOPICS);
  ASSERT_EQ(k.err, PackError::None);
  uint16_t out[8];
  ASSERT_EQ(k.c->titleSearch("  FIRE ", out, 8), 2);
  EXPECT_EQ(out[0], 2);
  EXPECT_EQ(out[1], 3);
  ASSERT_EQ(k.c->titleSearch("wood", out, 8), 1);  // only in a summary
  EXPECT_EQ(out[0], 3);
  ASSERT_EQ(k.c->titleSearch("help", out, 8), 2);  // title (call for help) before summary (threes mean help)
  EXPECT_EQ(out[0], 0);
  EXPECT_EQ(out[1], 1);
  EXPECT_EQ(k.c->titleSearch("   ", out, 8), 0);
  EXPECT_EQ(k.c->titleSearch("zzz", out, 8), 0);
}

TEST(GuideCatalog, ToleratesCommentsBlankLinesCrlfAndExtraColumns) {
  Cat k(
      std::string("# cats\n\r\nemergency\tEMERGENCY\tFirst\t1\textra\r\n"),
      std::string("\n# topics\na\temergency\tA\tQX\tt/a.gp\t1\tsum\tlater-column\r\nb\temergency\tB\t-\tt/b.gp\t2\t"));
  ASSERT_EQ(k.err, PackError::None);
  EXPECT_EQ(k.c->topicCount(), 2);
  EXPECT_TRUE(k.c->topic(0).quick());  // X: a later format's flag, ignored
  EXPECT_STREQ(k.c->topic(0).summary, "sum");
  EXPECT_STREQ(k.c->topic(1).summary, "");  // the last line has no '\n': terminated in the spare byte
  EXPECT_STREQ(k.c->category(0).blurb, "First");
}

TEST(GuideCatalog, RefusesEveryBrokenRule) {
  const std::string goodCat = "a\tA\tx\t1\nb\tB\ty\t2\n";
  const std::string goodTop = "t1\ta\tT1\t-\tt/t1.gp\t1\ts\nt2\tb\tT2\t-\tt/t2.gp\t1\ts\n";
  EXPECT_EQ(Cat(goodCat, goodTop).err, PackError::None);
  const std::pair<std::string, std::string> bad[] = {
      {"", goodTop},                                                            // no categories
      {goodCat, ""},                                                            // no topics
      {"a\tA\tx\t2\nb\tB\ty\t1\n", goodTop},                                    // order not 1, 2, ...
      {"a\tA\tx\t1\na\tB\ty\t2\n", goodTop},                                    // duplicate category
      {"A\tA\tx\t1\nb\tB\ty\t2\n", goodTop},                                    // bad category id
      {"a\t\tx\t1\nb\tB\ty\t2\n", goodTop},                                     // empty title
      {"a\tA\tx\nb\tB\ty\t2\n", goodTop},                                       // missing column
      {goodCat, "t1\ta\tT1\t-\tt/t1.gp\t1\n"},                                  // missing column
      {goodCat, "t1\tc\tT1\t-\tt/t1.gp\t1\ts\n"},                               // unknown category
      {goodCat, "t2\tb\tT2\t-\tt/t2.gp\t1\ts\nt1\ta\tT1\t-\tt/t1.gp\t1\ts\n"},  // categories out of order
      {goodCat, "t1\ta\tT1\t-\tt/t1.gp\t1\ts\nt1\tb\tT2\t-\tt/t2.gp\t1\ts\n"},  // duplicate topic id
      {goodCat, "T1\ta\tT1\t-\tt/t1.gp\t1\ts\n"},                               // bad topic id
      {goodCat, "t1\ta\t\t-\tt/t1.gp\t1\ts\n"},                                 // empty title
      {goodCat, "t1\ta\tT1\tq\tt/t1.gp\t1\ts\n"},                               // lower-case flag
      {goodCat, "t1\ta\tT1\t\tt/t1.gp\t1\ts\n"},                                // empty flags
      {goodCat, "t1\ta\tT1\t-\tt/../x.gp\t1\ts\n"},                             // path escape
      {goodCat, "t1\ta\tT1\t-\tt1.gp\t1\ts\n"},                                 // not under t/
      {goodCat, "t1\ta\tT1\t-\tt/t1.txt\t1\ts\n"},                              // not .gp
      {goodCat, "t1\ta\tT1\t-\tt/t1.gp\t0\ts\n"},                               // 0 pages
      {goodCat, "t1\ta\tT1\t-\tt/t1.gp\t65\ts\n"},                              // > MAX_PAGES
      {goodCat, "t1\ta\tT1\t-\tt/t1.gp\tx\ts\n"},                               // pages not a number
      {goodCat, "t1\ta\t" + std::string(MAX_TITLE + 1, 'T') + "\t-\tt/t1.gp\t1\ts\n"},
      {goodCat, "t1\ta\tT1\t-\tt/t1.gp\t1\t" + std::string(MAX_SUMMARY + 1, 's') + "\n"},
  };
  for (const auto& b : bad) {
    Cat k(b.first, b.second);
    EXPECT_EQ(k.err, PackError::Damaged) << b.first << "|" << b.second;
    EXPECT_EQ(k.c->topicCount(), 0);
    EXPECT_EQ(k.c->categoryCount(), 0);
  }
}

TEST(GuideCatalog, Caps) {
  std::string cats, topics;
  for (int i = 1; i <= MAX_CATEGORIES + 1; i++) cats += "c" + std::to_string(i) + "\tC\tb\t" + std::to_string(i) + "\n";
  topics = "t\tc1\tT\t-\tt/t.gp\t1\ts\n";
  EXPECT_EQ(Cat(cats, topics).err, PackError::Damaged);
  cats = "c\tC\tb\t1\n";
  topics.clear();
  for (int i = 0; i < MAX_TOPICS; i++) {
    const std::string id = "t" + std::to_string(i);
    topics += id + "\tc\tTitle\t-\tt/" + id + ".gp\t1\ts\n";
  }
  {
    Cat k(cats, topics);
    ASSERT_EQ(k.err, PackError::None);
    EXPECT_EQ(k.c->topicCount(), MAX_TOPICS);
    for (int i = 0; i < MAX_TOPICS; i += 97) EXPECT_EQ(k.c->findTopic("t" + std::to_string(i)), i);
  }
  topics += "extra\tc\tTitle\t-\tt/extra.gp\t1\ts\n";
  EXPECT_EQ(Cat(cats, topics).err, PackError::Damaged);
}

TEST(GuideCatalog, PathsAndIds) {
  char buf[96];
  ASSERT_GT(packPath("survival", TOPICS_FILE, buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "/Guides/survival/topics.tsv");
  ASSERT_GT(figurePath("survival", "atp-p125-1", buf, sizeof(buf)), 0u);
  EXPECT_STREQ(buf, "/Guides/survival/fig/L/atp-p125-1.png");
  ASSERT_GT(figurePath("survival", "atp-p125-1", buf, sizeof(buf), FIGURE_XL_DIR), 0u);
  EXPECT_STREQ(buf, "/Guides/survival/fig/XL/atp-p125-1.png");
  EXPECT_EQ(packPath("survival", TOPICS_FILE, buf, 10), 0u);
  EXPECT_TRUE(validId("a-b-9", MAX_ID));
  EXPECT_FALSE(validId("", MAX_ID));
  EXPECT_FALSE(validId("a_b", MAX_ID));
  EXPECT_FALSE(validId(std::string(MAX_ID + 1, 'a'), MAX_ID));
  EXPECT_TRUE(validTopicFile("t/fire-lays.gp"));
  EXPECT_FALSE(validTopicFile("t/.gp"));
  EXPECT_FALSE(validTopicFile("t/a/b.gp"));
  EXPECT_FALSE(validTopicFile("/t/a.gp"));
}

// ---- .gp pages ---------------------------------------------------------------------------------------

namespace {
const char* const GP =
    "= Stop the bleeding\n"
    "@fig atp-p25-1 Pressure dressing tied over a wound\n"
    "1. Make sure it is safe.\n"
    "2. **Press hard**, straight down.\n"
    "- a bullet\n"
    "! Do NOT remove it.\n"
    "* Reference only.\n"
    "Plain paragraph with **bold**.\n"
    "\\- not a bullet\n"
    "\n"
    "---\n"
    "= Second page\r\n"
    "12. twelve\n"
    "@fig2 is a paragraph\n";

struct Gp {
  Buf buf;
  std::unique_ptr<TopicText> t = std::make_unique<TopicText>();
  GpError err;
  explicit Gp(const std::string& s) : buf(s) { err = t->parse(buf.data(), buf.len); }
};
}  // namespace

TEST(GuidePage, ParsesEveryBlockKind) {
  Gp g(GP);
  ASSERT_EQ(g.err, GpError::None);
  const TopicText& t = *g.t;
  ASSERT_EQ(t.pageCount(), 2);
  const PageText& p0 = t.page(0);
  EXPECT_STREQ(p0.title, "Stop the bleeding");
  ASSERT_NE(p0.figure, nullptr);
  EXPECT_STREQ(p0.figure, "atp-p25-1");
  EXPECT_STREQ(p0.caption, "Pressure dressing tied over a wound");
  ASSERT_EQ(p0.blockCount, 7);
  const Block& s1 = t.block(p0.firstBlock);
  EXPECT_EQ(s1.kind, BlockKind::Step);
  EXPECT_EQ(s1.number, 1);
  EXPECT_EQ(std::string(s1.marker, s1.markerLen), "1.");
  EXPECT_STREQ(s1.text, "Make sure it is safe.");
  EXPECT_STREQ(t.block(1).text, "**Press hard**, straight down.");
  EXPECT_EQ(t.block(2).kind, BlockKind::Bullet);
  EXPECT_STREQ(t.block(2).text, "a bullet");
  EXPECT_EQ(t.block(3).kind, BlockKind::Warning);
  EXPECT_STREQ(t.block(3).text, "Do NOT remove it.");
  EXPECT_EQ(t.block(4).kind, BlockKind::Note);
  EXPECT_EQ(t.block(5).kind, BlockKind::Para);
  EXPECT_EQ(t.block(6).kind, BlockKind::Para);
  EXPECT_STREQ(t.block(6).text, "- not a bullet");
  const PageText& p1 = t.page(1);
  EXPECT_STREQ(p1.title, "Second page");
  EXPECT_EQ(p1.figure, nullptr);
  ASSERT_EQ(p1.blockCount, 2);
  EXPECT_EQ(t.block(p1.firstBlock).number, 12);
  EXPECT_EQ(std::string(t.block(p1.firstBlock).marker, t.block(p1.firstBlock).markerLen), "12.");
  EXPECT_EQ(t.block(p1.firstBlock + 1).kind, BlockKind::Para);
  EXPECT_EQ(t.clampPage(-1), 0);
  EXPECT_EQ(t.clampPage(9), 1);
}

TEST(GuidePage, FigureWithoutCaptionAndTitleOnlyPages) {
  Gp g("= A\n@fig x-1\n---\n= B\n---\n");
  ASSERT_EQ(g.err, GpError::None);
  ASSERT_EQ(g.t->pageCount(), 2);
  EXPECT_STREQ(g.t->page(0).figure, "x-1");
  EXPECT_STREQ(g.t->page(0).caption, "");
  EXPECT_EQ(g.t->page(1).blockCount, 0);
  Gp implied("= A\ntext\n= B\nmore");  // a title without "---" still opens a page; no final '\n'
  ASSERT_EQ(implied.err, GpError::None);
  EXPECT_EQ(implied.t->pageCount(), 2);
  EXPECT_STREQ(implied.t->block(1).text, "more");
}

TEST(GuidePage, RefusesEveryError) {
  const std::pair<std::string, GpError> bad[] = {
      {"", GpError::Empty},
      {"\n\n", GpError::Empty},
      {"text first\n= A\n", GpError::NoTitle},
      {"---\n= A\n", GpError::NoTitle},
      {"= A\n---\ntext without a title\n", GpError::NoTitle},
      {"= \n", GpError::NoTitle},
      {"= A\ntext\n@fig x\n", GpError::BadFigure},
      {"= A\n@fig x\n@fig y\n", GpError::BadFigure},
      {"= A\n@fig Bad_Name\n", GpError::BadFigure},
      {"= A\n@fig\n", GpError::BadFigure},
      {"= A\n- \n", GpError::BadLine},
      {"= A\n!  \n", GpError::BadLine},
      {"= A\n0. zero\n", GpError::BadLine},
      {"= A\n\\\n", GpError::BadLine},
  };
  for (const auto& b : bad) {
    Gp g(b.first);
    EXPECT_EQ(g.err, b.second) << b.first;
    EXPECT_EQ(g.t->pageCount(), 0);
  }
  std::string many;
  for (int i = 0; i <= MAX_PAGES; i++) many += "= P\n";
  EXPECT_EQ(Gp(many).err, GpError::TooBig);
  std::string blocks = "= P\n";
  for (int i = 0; i <= TopicText::MAX_BLOCKS; i++) blocks += "- x\n";
  EXPECT_EQ(Gp(blocks).err, GpError::TooBig);
  EXPECT_EQ(Gp(std::string(MAX_GP_BYTES + 1, 'x')).err, GpError::TooBig);
  Gp line("= A\nok\n- \n");
  EXPECT_EQ(line.t->errorLine(), 3);
}

TEST(GuidePage, StepPrefixRules) {
  Gp g("= A\n1.5 litres is a paragraph\n1000. too many digits\n3.no space\n7. seven\n");
  ASSERT_EQ(g.err, GpError::None);
  ASSERT_EQ(g.t->blockCount(), 4);
  EXPECT_EQ(g.t->block(0).kind, BlockKind::Para);
  EXPECT_EQ(g.t->block(1).kind, BlockKind::Para);
  EXPECT_EQ(g.t->block(2).kind, BlockKind::Para);
  EXPECT_EQ(g.t->block(3).kind, BlockKind::Step);
  EXPECT_EQ(g.t->block(3).number, 7);
}

// ---- PNG size --------------------------------------------------------------------------------------

TEST(GuidePng, ReadsTheHeader) {
  const uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0,    0, 13, 'I', 'H',
                         'D',  'R', 0,   0,   1,    0xB8, 0,    0,    1, 0x90, 1, 0,  0,   0};
  int w = 0, h = 0;
  ASSERT_TRUE(pngSize(png, sizeof(png), w, h));
  EXPECT_EQ(w, 440);
  EXPECT_EQ(h, 400);
  EXPECT_FALSE(pngSize(png, 23, w, h));
  uint8_t bad[sizeof(png)];
  for (size_t i = 0; i < 24; i++) {
    if (i >= 16 && i != 17 && i != 21) continue;  // width / height bytes may change (most stay valid)
    std::memcpy(bad, png, sizeof(png));
    bad[i] ^= 0x5A;
    EXPECT_FALSE(pngSize(bad, sizeof(bad), w, h)) << i;
  }
  std::memcpy(bad, png, sizeof(png));
  bad[18] = bad[19] = 0;
  EXPECT_FALSE(pngSize(bad, sizeof(bad), w, h));  // width 0
}

// ---- navigation --------------------------------------------------------------------------------------

namespace {
// Topics 0..4 as in CATS/TOPICS: pages 2, 1, 3, 2, 1.
struct NavFixture {
  Cat k{CATS, TOPICS};
  uint16_t seqBuf[8];
  Seq fire;
  NavFixture() { fire = categorySeq(*k.c, 1, seqBuf, 8); }
};
Pos pos(int t, int p, int s) {
  Pos x;
  x.topic = static_cast<int16_t>(t);
  x.page = static_cast<uint8_t>(p);
  x.sub = static_cast<uint8_t>(s);
  return x;
}
}  // namespace

TEST(GuideNav, Sequences) {
  NavFixture f;
  ASSERT_EQ(f.fire.count, 2);
  EXPECT_EQ(f.fire.topics[0], 2);
  EXPECT_EQ(f.fire.topics[1], 3);
  EXPECT_EQ(neighborTopic(f.fire, 2, +1), 3);
  EXPECT_EQ(neighborTopic(f.fire, 3, +1), -1);
  EXPECT_EQ(neighborTopic(f.fire, 3, -1), 2);
  EXPECT_EQ(neighborTopic(f.fire, 2, -1), -1);
  EXPECT_EQ(neighborTopic(f.fire, 0, +1), -1);  // not in the sequence
  uint16_t b[2];
  EXPECT_EQ(categorySeq(*f.k.c, 9, b, 2).count, 0);
  EXPECT_EQ(categorySeq(*f.k.c, 0, b, 1).count, 1);
}

TEST(GuideNav, NextWalksScreensPagesThenTopics) {
  NavFixture f;
  const Catalog& c = *f.k.c;
  Pos out;
  // fire-basics (3 pages): page 0 has 2 screens.
  EXPECT_EQ(nextPos(pos(2, 0, 0), 2, 3, f.fire, c, out), Step::SamePage);
  EXPECT_EQ(out, pos(2, 0, 1));
  EXPECT_EQ(nextPos(pos(2, 0, 1), 2, 3, f.fire, c, out), Step::OtherPage);
  EXPECT_EQ(out, pos(2, 1, 0));
  EXPECT_EQ(nextPos(pos(2, 2, 0), 1, 3, f.fire, c, out), Step::OtherTopic);
  EXPECT_EQ(out, pos(3, 0, 0));
  EXPECT_EQ(nextPos(pos(3, 1, 0), 1, 2, f.fire, c, out), Step::None);  // the category's last screen
  EXPECT_EQ(out, pos(3, 1, 0));
  // SUB_LAST resolves before stepping.
  EXPECT_EQ(nextPos(pos(2, 0, SUB_LAST), 2, 3, f.fire, c, out), Step::OtherPage);
  Pos none;
  EXPECT_EQ(nextPos(none, 1, 1, f.fire, c, out), Step::None);
}

TEST(GuideNav, PrevWalksBackToTheLastScreenOfThePreviousTopic) {
  NavFixture f;
  const Catalog& c = *f.k.c;
  Pos out;
  EXPECT_EQ(prevPos(pos(3, 0, 1), 2, 2, f.fire, c, out), Step::SamePage);
  EXPECT_EQ(out, pos(3, 0, 0));
  EXPECT_EQ(prevPos(pos(3, 1, 0), 1, 2, f.fire, c, out), Step::OtherPage);
  EXPECT_EQ(out, pos(3, 0, SUB_LAST));
  EXPECT_EQ(prevPos(pos(3, 0, 0), 2, 2, f.fire, c, out), Step::OtherTopic);
  EXPECT_EQ(out, pos(2, 2, SUB_LAST));  // fire-basics has 3 pages: its last page, its last screen
  EXPECT_EQ(prevPos(pos(2, 0, 0), 1, 3, f.fire, c, out), Step::None);
  EXPECT_EQ(resolveSub(SUB_LAST, 3), 2);
  EXPECT_EQ(resolveSub(SUB_LAST, 1), 0);
  EXPECT_EQ(resolveSub(1, 3), 1);
  EXPECT_EQ(resolveSub(7, 3), 2);
  EXPECT_EQ(resolveSub(4, 0), 0);
  // A stale page past the topic's end steps back inside it.
  EXPECT_EQ(prevPos(pos(3, 9, 0), 1, 2, f.fire, c, out), Step::OtherPage);
  EXPECT_EQ(out.page, 1);
}

TEST(GuideNav, ScreenCounter) {
  const uint8_t per[3] = {2, 1, 3};
  EXPECT_EQ(screenTotal(per, 3), 6);
  EXPECT_EQ(screenOrdinal(pos(0, 0, 0), per, 3), 1);
  EXPECT_EQ(screenOrdinal(pos(0, 0, 1), per, 3), 2);
  EXPECT_EQ(screenOrdinal(pos(0, 1, 0), per, 3), 3);
  EXPECT_EQ(screenOrdinal(pos(0, 2, SUB_LAST), per, 3), 6);
  uint8_t page = 9, sub = 9;
  for (int n = 1; n <= 6; n++) {
    ASSERT_TRUE(posOfOrdinal(n, per, 3, page, sub));
    EXPECT_EQ(screenOrdinal(pos(0, page, sub), per, 3), n);
  }
  EXPECT_FALSE(posOfOrdinal(0, per, 3, page, sub));
  EXPECT_FALSE(posOfOrdinal(7, per, 3, page, sub));
}
