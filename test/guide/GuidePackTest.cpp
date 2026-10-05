// The real survival guide device pack (packs/guide/build/survival, made by scripts/guide/make_pack.py)
// through lib/Guide with the device's own fonts (SleepCardHost: the real GfxRenderer and built-in
// fonts) and the page screen's metrics adapter (src/activities/apps/GuideMetrics):
//   - pack.txt, categories.tsv and topics.tsv parse; EMERGENCY comes first; caps hold;
//   - every topic's .gp parses with the page count topics.tsv gives; every figure is a 1-bit PNG
//     within 440 x 480 that the layout can size;
//   - every page lays out in at most 3 screens (medical topics with the reference-only lead on their
//     first page), never truncated, every run inside the text column and the band; every QUICK
//     CARD fits ONE screen;
//   - search.idx attaches and finds every topic by its own title; a few everyday queries rank the
//     right topic first;
//   - every truncation and thousands of random corruptions of every file are refused or parse into
//     something that lays out within its caps - never a crash; a newer format is refused as such.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "CardPreview.h"
#include "GuideTestUtil.h"
#include "activities/apps/GuideMetrics.h"

using namespace gd;
using guidetest::Buf;
using guidetest::Pack;
using guidetest::PackSizer;
using guidetest::readFile;

namespace {

constexpr int MAX_SCREENS_PER_PAGE = 3;

const Pack& pack() {
  static Pack p(GUIDE_PACK);
  return p;
}

const char* leadFor(const Topic& t, int page) { return (t.medical() && page == 0) ? pack().info.refNote : nullptr; }

const RendererMetrics& metrics() {
  static RendererMetrics m(sleepcards::preview::renderer(), pageFonts());
  return m;
}

const RendererMetrics& compactMetrics() {
  static RendererMetrics m(sleepcards::preview::renderer(), compactFonts());
  return m;
}

struct Fit {
  int screens = 0;
  int lastBottom = 0;
};

// Lays out every page of topic i with the given fonts and geometry, checking every run and shape.
Fit fitTopic(int i, const RendererMetrics& m, const Geometry& g, std::vector<int>* perPage = nullptr) {
  const Pack& p = pack();
  const Topic& t = p.catalog->topic(i);
  const PackSizer sizer(GUIDE_PACK);
  Buf buf;
  auto text = std::make_unique<TopicText>();
  EXPECT_EQ(p.loadTopic(i, buf, *text), GpError::None) << t.id;
  auto layout = std::make_unique<PageLayout>();
  Fit fit;
  for (int pg = 0; pg < text->pageCount(); pg++) {
    const char* lead = leadFor(t, pg);
    const int n = layoutPage(*text, pg, lead, m, g, &sizer, layout.get());
    EXPECT_EQ(layoutPage(*text, pg, lead, m, g, &sizer, nullptr), n) << t.id << " p" << pg;
    EXPECT_FALSE(layout->truncated()) << t.id << " p" << pg;
    if (perPage) perPage->push_back(n);
    fit.screens += n;
    fit.lastBottom = layout->screen(n - 1).bottom;
    for (int r = 0; r < layout->runCount(); r++) {
      const TextRun& run = layout->run(r);
      const int w = m.textWidth(run.font, run.text, run.len);
      EXPECT_GE(run.x, 0) << t.id;
      EXPECT_LE(run.x + w, g.left + g.width) << t.id << " p" << pg << " \"" << std::string(run.text, run.len) << "\"";
      EXPECT_GE(run.y, g.top);
      EXPECT_LE(run.y + m.lineHeight(run.font), g.bottom) << t.id;
    }
    for (int k = 0; k < layout->shapeCount(); k++) {
      const Shape& sh = layout->shape(k);
      EXPECT_EQ(sh.flags & FIGURE_MISSING, 0) << t.id << ": figure " << (sh.ref ? sh.ref : "");
      EXPECT_LE(sh.y + sh.h, g.bottom) << t.id;
    }
  }
  uint8_t per[MAX_PAGES];
  EXPECT_EQ(gd::topicScreens(*text, leadFor(t, 0), m, g, &sizer, per, MAX_PAGES), fit.screens) << t.id;
  return fit;
}

}  // namespace

TEST(GuideRealPack, FontMetricsAreSane) {
  for (const RendererMetrics* m : {&metrics(), &compactMetrics()}) {
    for (const Font f : {Font::Body, Font::Bold, Font::Title, Font::Lead, Font::Crumb}) {
      const int lh = m->lineHeight(f), sp = m->spaceWidth(f), w = m->textWidth(f, "hello world", 11);
      std::printf("[guide] %s font %d: line %d, space %d, \"hello world\" %d px\n",
                  m == &metrics() ? "page" : "compact", static_cast<int>(f), lh, sp, w);
      EXPECT_GT(lh, 10);
      EXPECT_LT(lh, 60);
      EXPECT_GT(sp, 0);
      EXPECT_GT(w, 40);
      EXPECT_LT(w, 200);
    }
  }
  // A long run measures in pieces: about the sum of its halves.
  const std::string longWord(300, 'm');
  const int whole = metrics().textWidth(Font::Body, longWord.data(), longWord.size());
  const int half = metrics().textWidth(Font::Body, longWord.data(), 150);
  EXPECT_NEAR(whole, 2 * half, 4);
}

TEST(GuideRealPack, PackInfoAndCatalog) {
  const Pack& p = pack();
  ASSERT_EQ(p.infoError, PackError::None) << "run scripts/guide/make_pack.py";
  EXPECT_EQ(p.info.format, FORMAT);
  EXPECT_STREQ(p.info.id, "survival");
  EXPECT_STREQ(p.info.title, "Survival Guide");
  EXPECT_STREQ(p.info.shortTitle, "SURVIVAL");
  EXPECT_STREQ(p.info.status, "reviewed-by-ai");
  EXPECT_NE(std::string(p.info.statusText).find("not by a medical professional"), std::string::npos);
  EXPECT_NE(std::string(p.info.note).find("911"), std::string::npos);
  EXPECT_NE(std::string(p.info.refNote).find("Reference only"), std::string::npos);
  // No field was cut.
  const std::string packTxt = readFile(std::string(GUIDE_PACK) + "/pack.txt");
  EXPECT_NE(packTxt.find(std::string("note=") + p.info.note + "\n"), std::string::npos);
  EXPECT_NE(packTxt.find(std::string("status_text=") + p.info.statusText + "\n"), std::string::npos);
  EXPECT_NE(packTxt.find(std::string("ref_note=") + p.info.refNote + "\n"), std::string::npos);

  ASSERT_EQ(p.catalogError, PackError::None);
  const Catalog& c = *p.catalog;
  EXPECT_EQ(c.categoryCount(), 12);
  EXPECT_STREQ(c.category(0).id, "emergency");
  EXPECT_STREQ(c.category(0).title, "EMERGENCY");
  EXPECT_EQ(c.topicCount(), 86);
  int medical = 0;
  for (int i = 0; i < c.topicCount(); i++) medical += c.topic(i).medical() ? 1 : 0;
  EXPECT_EQ(c.quickCount(), 10);
  EXPECT_EQ(medical, 21);
  for (int k = 0; k < c.categoryCount(); k++) EXPECT_GT(c.category(k).count, 0) << c.category(k).id;
}

TEST(GuideRealPack, EveryTopicParsesEveryFigureIsSized) {
  const Pack& p = pack();
  ASSERT_EQ(p.catalogError, PackError::None);
  const PackSizer sizer(GUIDE_PACK);
  std::set<std::string> used;
  for (int i = 0; i < p.catalog->topicCount(); i++) {
    const Topic& t = p.catalog->topic(i);
    Buf buf;
    auto text = std::make_unique<TopicText>();
    ASSERT_EQ(p.loadTopic(i, buf, *text), GpError::None) << t.id << " line " << text->errorLine();
    EXPECT_LE(buf.len, MAX_GP_BYTES);
    EXPECT_EQ(text->pageCount(), t.pages) << t.id;
    if (t.quick()) EXPECT_EQ(text->pageCount(), 1) << t.id;
    for (int pg = 0; pg < text->pageCount(); pg++) {
      const PageText& page = text->page(pg);
      if (!page.figure) continue;
      used.insert(page.figure);
      const std::string png = readFile(std::string(GUIDE_PACK) + "/fig/L/" + page.figure + ".png");
      int w = 0, h = 0;
      ASSERT_TRUE(pngSize(reinterpret_cast<const uint8_t*>(png.data()), png.size(), w, h)) << page.figure;
      EXPECT_LE(w, MAX_FIG_W) << page.figure;
      EXPECT_LE(h, MAX_FIG_H) << page.figure;
      EXPECT_EQ(static_cast<uint8_t>(png[24]), 1) << page.figure << ": 1 bit a pixel";
      EXPECT_EQ(static_cast<uint8_t>(png[25]), 0) << page.figure << ": grayscale";
      EXPECT_NE(page.caption, nullptr);
    }
  }
  // Every figure in the pack is used by a page (the builder writes only those).
  int files = 0;
  for (const auto& e : std::filesystem::directory_iterator(std::string(GUIDE_PACK) + "/fig/L")) {
    files++;
    EXPECT_TRUE(used.count(e.path().stem().string())) << e.path();
  }
  EXPECT_EQ(files, static_cast<int>(used.size()));
}

// The full-screen view's rasters (fig/XL): each one is a used figure's, fits the view's box, is 1-bit,
// and is clearly larger than the page's (else the builder leaves it out and the view shows fig/L).
// Most figures are wide, so most are turned sideways: the long axis runs down the screen.
TEST(GuideRealPack, FullScreenFiguresAreLarger) {
  const PackSizer page(GUIDE_PACK);
  const PackSizer full(GUIDE_PACK, "fig/XL");
  int files = 0, turned = 0;
  double smallest = 99;
  for (const auto& e : std::filesystem::directory_iterator(std::string(GUIDE_PACK) + "/fig/XL")) {
    files++;
    const std::string name = e.path().stem().string();
    int pw = 0, ph = 0, fw = 0, fh = 0;
    ASSERT_TRUE(page.size(name.c_str(), pw, ph)) << name << ": no page figure";
    ASSERT_TRUE(full.size(name.c_str(), fw, fh)) << name;
    EXPECT_LE(fw, MAX_FIG_XL_W) << name;
    EXPECT_LE(fh, MAX_FIG_XL_H) << name;
    const std::string png = readFile(e.path().string());
    EXPECT_EQ(static_cast<uint8_t>(png[24]), 1) << name << ": 1 bit a pixel";
    const bool isTurned = (pw > ph) != (fw > fh);
    if (isTurned) turned++;
    // The scale against the page's figure, along the same axis of the drawing.
    const double gain = isTurned ? static_cast<double>(fh) / pw : static_cast<double>(fw) / pw;
    EXPECT_GE(gain, 1.14) << name << ": " << pw << "x" << ph << " -> " << fw << "x" << fh;
    smallest = std::min(smallest, gain);
  }
  EXPECT_GE(files, 60) << "nearly every figure has a full-screen raster";
  EXPECT_GE(turned, 30);
  std::printf("[ fig/XL   ] %d full-screen figures (%d turned), smallest gain %.2fx\n", files, turned, smallest);
}

TEST(GuideRealPack, EveryPageFitsThreeScreens) {
  const Pack& p = pack();
  ASSERT_EQ(p.catalogError, PackError::None);
  const Geometry g;
  int pages = 0, worst = 0;
  int histogram[MAX_SCREENS_PER_PAGE + 2] = {};
  std::string report;
  for (int i = 0; i < p.catalog->topicCount(); i++) {
    const Topic& t = p.catalog->topic(i);
    std::vector<int> per;
    fitTopic(i, metrics(), g, &per);
    for (size_t pg = 0; pg < per.size(); pg++) {
      const int n = per[pg];
      pages++;
      worst = std::max(worst, n);
      histogram[std::min(n, MAX_SCREENS_PER_PAGE + 1)]++;
      EXPECT_LE(n, MAX_SCREENS_PER_PAGE) << t.id << " page " << pg + 1;
      if (n >= MAX_SCREENS_PER_PAGE) report += std::string("  ") + t.id + " p" + std::to_string(pg + 1) + "\n";
    }
  }
  std::printf("[guide] page style: %d pages; %d fit one screen, %d need 2, %d need 3, %d more; worst %d\n%s", pages,
              histogram[1], histogram[2], histogram[3], histogram[4], worst, report.c_str());
}

// The trade-off, for the maintainer to sign off (or reverse): quick cards are set in the compact
// style (UI 10 body, ~24 px lines, against NotoSans 12's 34 px on a normal page) so that each fits
// ONE screen. In the page style every card needs 2-3 screens; at UI 12 every card needs 2. A larger
// quick-card type means rewriting every card to roughly half its words.
TEST(GuideRealPack, QuickCardsFitOneScreenInTheCompactStyle) {
  const Pack& p = pack();
  ASSERT_EQ(p.catalogError, PackError::None);
  const Geometry g = compactGeometry();
  int quick = 0;
  for (int i = 0; i < p.catalog->topicCount(); i++) {
    const Topic& t = p.catalog->topic(i);
    if (!t.quick()) continue;
    quick++;
    const Fit compact = fitTopic(i, compactMetrics(), g);
    const Fit normal = fitTopic(i, metrics(), Geometry{});
    std::printf("[guide] quick card %-16s compact %d screen(s)%s, page style %d\n", t.id, compact.screens,
                compact.screens > 1 ? (" (the last ends at y " + std::to_string(compact.lastBottom) + ")").c_str() : "",
                normal.screens);
    // Every quick card, no exceptions: a card that does not fit is trimmed, or made a normal topic.
    EXPECT_EQ(compact.screens, 1) << "quick card " << t.id << " needs " << compact.screens << " screens";
  }
  EXPECT_EQ(quick, 10);
}

TEST(GuideRealPack, AboutLaysOut) {
  Buf buf(readFile(std::string(GUIDE_PACK) + "/about.txt"));
  ASSERT_GT(buf.len, 0u);
  EXPECT_LE(buf.len, MAX_ABOUT_BYTES);
  auto text = std::make_unique<TopicText>();
  ASSERT_EQ(text->parse(buf.data(), buf.len), GpError::None);
  EXPECT_EQ(text->pageCount(), 2);
  auto layout = std::make_unique<PageLayout>();
  for (int pg = 0; pg < text->pageCount(); pg++) {
    const int n = layoutPage(*text, pg, nullptr, metrics(), Geometry{}, nullptr, layout.get());
    EXPECT_GE(n, 1);
    EXPECT_LE(n, MAX_SCREENS_PER_PAGE);
    EXPECT_FALSE(layout->truncated());
  }
}

TEST(GuideRealPack, SearchFindsEveryTopicByItsTitle) {
  const Pack& p = pack();
  ASSERT_EQ(p.catalogError, PackError::None);
  const std::string idx = readFile(std::string(GUIDE_PACK) + "/search.idx");
  ASSERT_LE(idx.size(), MAX_INDEX_BYTES);
  auto index = std::make_unique<SearchIndex>();
  ASSERT_TRUE(index->attach(idx.data(), idx.size()));
  EXPECT_GT(index->termCount(), 500);
  Hit hits[64];
  for (int i = 0; i < p.catalog->topicCount(); i++) {
    const Topic& t = p.catalog->topic(i);
    const int n = index->query(t.title, *p.catalog, hits, 64);
    bool found = false;
    for (int k = 0; k < n; k++) found = found || hits[k].topic == i;
    EXPECT_TRUE(found) << "searching \"" << t.title << "\" misses " << t.id;
  }
  // Everyday queries: the right topic first.
  const std::pair<const char*, const char*> top[] = {
      {"tourniquet", "bleeding-and-shock"},
      {"bow drill", "friction-fire"},
      {"snakebite", "snakebite"},
      {"fire lays", "fire-lays"},
      {"clove hitch", "hitches"},
      {"bear", "bears"},
      {"cougar", "cougars"},
      {"lightning", "lightning"},
      {"death cap", "death-cap"},
  };
  for (const auto& q : top) {
    const int n = index->query(q.first, *p.catalog, hits, 64);
    ASSERT_GT(n, 0) << q.first;
    EXPECT_STREQ(p.catalog->topic(hits[0].topic).id, q.second) << "searching \"" << q.first << "\"";
  }
  // Synonyms reach the right topic.
  const int n = index->query("blood", *p.catalog, hits, 64);
  bool bleeding = false;
  for (int k = 0; k < n; k++)
    bleeding = bleeding || std::string(p.catalog->topic(hits[k].topic).id) == "bleeding-and-shock";
  EXPECT_TRUE(bleeding);
}

TEST(GuideRealPack, NewerFormatIsRefusedAsSuch) {
  std::string packTxt = readFile(std::string(GUIDE_PACK) + "/pack.txt");
  const size_t at = packTxt.find("format=1");
  ASSERT_NE(at, std::string::npos);
  packTxt.replace(at, 8, "format=2");
  PackInfo info;
  EXPECT_EQ(parsePackInfo(packTxt.data(), packTxt.size(), info), PackError::NeedsNewerFirmware);
}

// ---- corruption: refused or parsed into something that lays out within its caps, never a crash ----

namespace {

std::vector<std::string> mutations(const std::string& good, const int count, const unsigned seed) {
  std::mt19937 rng(seed);
  std::vector<std::string> out;
  // Truncations: every length for a small file, a spread for a big one.
  const size_t step = good.size() <= 4096 ? 1 : good.size() / 512;
  for (size_t n = 0; n < good.size(); n += step) out.push_back(good.substr(0, n));
  for (int i = 0; i < count; i++) {
    std::string m = good;
    const int edits = 1 + static_cast<int>(rng() % 4);
    for (int e = 0; e < edits; e++) {
      const size_t at = rng() % m.size();
      switch (rng() % 4) {
        case 0:
          m[at] = static_cast<char>(rng() % 256);
          break;
        case 1:
          m[at] = "\t\n-=!*@#:,\\ "[rng() % 13];
          break;
        case 2:
          m.erase(at, 1 + rng() % 8);
          break;
        default:
          m.insert(at, m.substr(rng() % m.size(), 1 + rng() % 40));
          break;
      }
      if (m.empty()) m = "x";
    }
    out.push_back(m);
  }
  return out;
}

void layoutEverything(const TopicText& text, const FontMetrics& m) {
  auto layout = std::make_unique<PageLayout>();
  for (int pg = 0; pg < text.pageCount(); pg++) {
    const int n = layoutPage(text, pg, "lead", m, Geometry{}, nullptr, layout.get());
    ASSERT_GE(n, 1);
    ASSERT_LE(n, PageLayout::MAX_SCREENS);
    ASSERT_LE(layout->runCount(), PageLayout::MAX_RUNS);
  }
}

}  // namespace

TEST(GuideRealPackCorruption, PackTxt) {
  const std::string good = readFile(std::string(GUIDE_PACK) + "/pack.txt");
  for (const std::string& m : mutations(good, 3000, 1)) {
    PackInfo info;
    const PackError e = parsePackInfo(m.data(), m.size(), info);
    if (e == PackError::None) {
      EXPECT_TRUE(validId(info.id, MAX_ID));
      EXPECT_GE(info.format, 1);
      EXPECT_LE(info.format, FORMAT);
    }
  }
}

TEST(GuideRealPackCorruption, CatalogFiles) {
  const std::string cats = readFile(std::string(GUIDE_PACK) + "/categories.tsv");
  const std::string topics = readFile(std::string(GUIDE_PACK) + "/topics.tsv");
  auto c = std::make_unique<Catalog>();
  int parsed = 0;
  for (const std::string& m : mutations(cats, 1500, 2)) {
    Buf a(m), b(topics);
    if (c->parse(a.data(), a.len, b.data(), b.len) == PackError::None) parsed++;
  }
  for (const std::string& m : mutations(topics, 2000, 3)) {
    Buf a(cats), b(m);
    if (c->parse(a.data(), a.len, b.data(), b.len) == PackError::None) {
      parsed++;
      for (int k = 0; k < c->categoryCount(); k++) {
        const Category& cat = c->category(k);
        ASSERT_LE(cat.first + cat.count, c->topicCount());
      }
      for (int i = 0; i < c->topicCount(); i++) {
        ASSERT_TRUE(validTopicFile(c->topic(i).file));
        ASSERT_EQ(c->findTopic(c->topic(i).id), i);
      }
    }
  }
  EXPECT_GT(parsed, 0);  // some edits (inside a title, say) are still a valid catalog
}

TEST(GuideRealPackCorruption, PageFiles) {
  const char* const files[] = {"t/bleeding-and-shock.gp", "t/fire-lays.gp", "t/heat.gp", "t/cold-injuries.gp"};
  for (const char* f : files) {
    const std::string good = readFile(std::string(GUIDE_PACK) + "/" + f);
    ASSERT_FALSE(good.empty()) << f;
    for (const std::string& m : mutations(good, 1500, 4)) {
      Buf b(m);
      auto text = std::make_unique<TopicText>();
      if (text->parse(b.data(), b.len) != GpError::None) continue;
      layoutEverything(*text, guidetest::FakeMetrics{});
      layoutEverything(*text, metrics());  // the real renderer measures whatever bytes survive
    }
  }
}

TEST(GuideRealPackCorruption, SearchIndex) {
  const Pack& p = pack();
  ASSERT_EQ(p.catalogError, PackError::None);
  const std::string good = readFile(std::string(GUIDE_PACK) + "/search.idx");
  auto index = std::make_unique<SearchIndex>();
  Hit hits[32];
  int attached = 0;
  for (const std::string& m : mutations(good, 400, 5)) {
    if (!index->attach(m.data(), m.size())) continue;
    attached++;
    for (const char* q : {"fire", "water bleed", "a", "zz", "tourniquet"}) {
      const int n = index->query(q, *p.catalog, hits, 32);
      for (int k = 0; k < n; k++) ASSERT_TRUE(p.catalog->validTopic(hits[k].topic));
    }
  }
  EXPECT_GT(attached, 0);
}
