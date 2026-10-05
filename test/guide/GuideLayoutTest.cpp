// Survival guide layout on the host with a fake font (every byte 10 px; Body lines 20 px, Title 30,
// Lead 16): positions of every element, bold runs, list gaps, boxes (whole, moved, split), figures
// (scaled, centred, missing), the lead line, follow-on screens, the screen cap, overlong words, the
// count-only mode agreeing with the stored layout, and the breadcrumb.
#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <string>

#include "GuideTestUtil.h"

using namespace gd;
using guidetest::Buf;
using guidetest::FakeMetrics;
using guidetest::MapSizer;

namespace {

// The numbers below are worked out for this band (pinned, so a change to the defaults does not move them).
Geometry testGeometry() {
  Geometry g;
  g.top = 52;
  g.bottom = 728;
  g.leadGap = 10;
  return g;
}

struct Laid {
  Buf buf;
  std::unique_ptr<TopicText> text = std::make_unique<TopicText>();
  std::unique_ptr<PageLayout> layout = std::make_unique<PageLayout>();
  int screens = 0;
  Laid(const std::string& gp, const FigureSizer* sizer = nullptr, const char* lead = nullptr, int page = 0,
       const Geometry& g = testGeometry()) {
    buf.set(gp);
    EXPECT_EQ(text->parse(buf.data(), buf.len), GpError::None) << gp;
    const FakeMetrics m;
    screens = layoutPage(*text, page, lead, m, g, sizer, layout.get());
    // The count-only pass always agrees.
    EXPECT_EQ(layoutPage(*text, page, lead, m, g, sizer, nullptr), screens);
  }
  std::string runText(int i) const { return std::string(layout->run(i).text, layout->run(i).len); }
  const TextRun& run(int i) const { return layout->run(i); }
  int screenOfRun(int i) const {
    for (int s = 0; s < layout->screenCount(); s++) {
      const ScreenSpan& sp = layout->screen(s);
      if (i >= sp.firstRun && i < sp.firstRun + sp.runCount) return s;
    }
    return -1;
  }
};

std::string words(int n, const char* w = "aaaa") {
  std::string s;
  for (int i = 0; i < n; i++) s += (i ? " " : "") + std::string(w);
  return s;
}

// Every run inside the text column and the band.
void expectInside(const Laid& l, const Geometry& g = testGeometry()) {
  const FakeMetrics m;
  for (int i = 0; i < l.layout->runCount(); i++) {
    const TextRun& r = l.run(i);
    EXPECT_GE(r.x, g.left) << i;
    EXPECT_LE(r.x + m.textWidth(r.font, r.text, r.len), g.left + g.width) << i;
    EXPECT_GE(r.y, g.top) << i;
    EXPECT_LE(r.y + m.lineHeight(r.font), g.bottom) << i;
  }
  for (int i = 0; i < l.layout->shapeCount(); i++) {
    const Shape& s = l.layout->shape(i);
    EXPECT_GE(s.x, g.left);
    EXPECT_LE(s.x + s.w, g.left + g.width);
    EXPECT_GE(s.y, g.top);
    EXPECT_LE(s.y + s.h, g.bottom);
  }
}

}  // namespace

TEST(GuideLayout, TitleThenParagraph) {
  Laid l("= T\nHello world\n");
  ASSERT_EQ(l.screens, 1);
  ASSERT_EQ(l.layout->runCount(), 3);
  EXPECT_EQ(l.runText(0), "T");
  EXPECT_EQ(l.run(0).font, Font::Title);
  EXPECT_EQ(l.run(0).x, 20);
  EXPECT_EQ(l.run(0).y, 52);
  EXPECT_EQ(l.runText(1), "Hello");
  EXPECT_EQ(l.run(1).y, 52 + 30 + 10);  // title line + titleGap
  EXPECT_EQ(l.run(1).x, 20);
  EXPECT_EQ(l.run(2).x, 20 + 50 + 10);
  EXPECT_EQ(l.layout->screen(0).bottom, 92 + 20);
  EXPECT_FALSE(l.layout->truncated());
}

TEST(GuideLayout, WrapsAtTheColumnWidth) {
  // 4-letter words: 9 fit exactly in 440 px (9 * 40 + 8 * 10), the 10th wraps.
  Laid l("= T\n" + words(10) + "\n");
  ASSERT_EQ(l.layout->runCount(), 11);
  EXPECT_EQ(l.run(9).y, 92);
  EXPECT_EQ(l.run(9).x, 20 + 8 * 50);
  EXPECT_EQ(l.run(10).y, 112);
  EXPECT_EQ(l.run(10).x, 20);
  expectInside(l);
}

TEST(GuideLayout, BoldRunsAndMidWordToggles) {
  Laid l("= T\na **bold** b x**y**z ** c\n");
  ASSERT_EQ(l.layout->runCount(), 8);
  EXPECT_EQ(l.runText(1), "a");
  EXPECT_EQ(l.run(1).font, Font::Body);
  EXPECT_EQ(l.runText(2), "bold");
  EXPECT_EQ(l.run(2).font, Font::Bold);
  EXPECT_EQ(l.run(2).x, 40);
  EXPECT_EQ(l.runText(3), "b");
  EXPECT_EQ(l.run(3).font, Font::Body);
  EXPECT_EQ(l.run(3).x, 90);
  // x**y**z: three runs side by side, no space.
  EXPECT_EQ(l.runText(4), "x");
  EXPECT_EQ(l.runText(5), "y");
  EXPECT_EQ(l.run(5).font, Font::Bold);
  EXPECT_EQ(l.run(5).x, l.run(4).x + 10);
  EXPECT_EQ(l.runText(6), "z");
  EXPECT_EQ(l.run(6).x, l.run(5).x + 10);
  // A lone "**" toggles bold and takes no space: "c" is bold, one space after "z".
  EXPECT_EQ(l.runText(7), "c");
  EXPECT_EQ(l.run(7).font, Font::Bold);
  EXPECT_EQ(l.run(7).x, l.run(6).x + 20);
}

TEST(GuideLayout, BoldCarriesAcrossAWrap) {
  Laid l("= T\n" + words(8) + " **bold words here** after\n");
  // "bold" fits on line 1 (9th word); "words" wraps, still bold.
  int i = 0;
  while (i < l.layout->runCount() && l.runText(i) != "words") i++;
  ASSERT_LT(i, l.layout->runCount());
  EXPECT_EQ(l.run(i).font, Font::Bold);
  EXPECT_EQ(l.run(i).x, 20);
  EXPECT_EQ(l.run(i + 2).font, Font::Body);  // "after"
}

TEST(GuideLayout, BulletsStepsAndListGaps) {
  Laid l("= T\n- one\n- two\n1. first\n12. twelfth\nafter\n");
  ASSERT_EQ(l.layout->shapeCount(), 2);  // two dots
  const Shape& d0 = l.layout->shape(0);
  EXPECT_EQ(d0.kind, ShapeKind::Dot);
  EXPECT_EQ(d0.x, 28);
  EXPECT_EQ(d0.y, 92 + 7);
  EXPECT_EQ(d0.w, 6);
  // Bullet text at the indent; the next bullet one itemGap below.
  EXPECT_EQ(l.runText(1), "one");
  EXPECT_EQ(l.run(1).x, 50);
  EXPECT_EQ(l.runText(2), "two");
  EXPECT_EQ(l.run(2).y, 92 + 20 + 6);
  EXPECT_EQ(l.layout->shape(1).y, 118 + 7);
  // A step list after a bullet list: blockGap. The number right-aligned before the indent.
  EXPECT_EQ(l.runText(3), "1.");
  EXPECT_EQ(l.run(3).font, Font::Bold);
  EXPECT_EQ(l.run(3).x, 50 - 8 - 20);
  EXPECT_EQ(l.run(3).y, 118 + 20 + 12);
  EXPECT_EQ(l.runText(4), "first");
  EXPECT_EQ(l.run(4).x, 50);
  EXPECT_EQ(l.runText(5), "12.");
  EXPECT_EQ(l.run(5).x, 50 - 8 - 30);
  EXPECT_EQ(l.run(5).y, 150 + 20 + 6);
  EXPECT_EQ(l.runText(7), "after");
  EXPECT_EQ(l.run(7).y, 176 + 20 + 12);
  EXPECT_EQ(l.run(7).x, 20);
}

TEST(GuideLayout, BoxesCarryTheirLabel) {
  Laid l("= T\n! Do it\n* Note this\n");
  ASSERT_EQ(l.layout->shapeCount(), 2);
  const Shape& w = l.layout->shape(0);
  EXPECT_EQ(w.kind, ShapeKind::WarningBox);
  EXPECT_EQ(w.flags, EDGE_TOP | EDGE_BOTTOM);
  EXPECT_EQ(w.x, 20);
  EXPECT_EQ(w.w, 440);
  EXPECT_EQ(w.y, 92);
  EXPECT_EQ(w.h, 2 * 10 + 20);
  EXPECT_EQ(l.runText(1), "WARNING:");
  EXPECT_EQ(l.run(1).font, Font::Bold);
  EXPECT_EQ(l.run(1).x, 30);
  EXPECT_EQ(l.run(1).y, 102);
  EXPECT_EQ(l.runText(2), "Do");
  EXPECT_EQ(l.run(2).x, 30 + 80 + 10);
  const Shape& n = l.layout->shape(1);
  EXPECT_EQ(n.kind, ShapeKind::NoteBox);
  EXPECT_EQ(n.y, 92 + 40 + 12);
  EXPECT_EQ(l.runText(4), "NOTE:");
  expectInside(l);
}

TEST(GuideLayout, ParagraphContinuesOnAFollowOnScreen) {
  // 9 words a line; 40 lines. Screen 0 holds (728 - 92) / 20 = 31 lines, screen 1 the other 9.
  Laid l("= T\n" + words(9 * 40) + "\n");
  ASSERT_EQ(l.screens, 2);
  const ScreenSpan& s1 = l.layout->screen(1);
  EXPECT_EQ(l.layout->screen(0).runCount, 1 + 31 * 9);
  EXPECT_EQ(s1.runCount, 9 * 9);
  EXPECT_EQ(l.run(s1.firstRun).y, 52);  // follow-on screens start at the band's top
  EXPECT_EQ(s1.bottom, 52 + 9 * 20);
  expectInside(l);
}

TEST(GuideLayout, BoxMovesWholeOrSplits) {
  // 30 lines of paragraph leave 728 - 692 = 36 px: a 40 px box moves to the next screen whole.
  Laid moved("= T\n" + words(9 * 30) + "\n! Do it\n");
  ASSERT_EQ(moved.screens, 2);
  const Shape& b = moved.layout->shape(0);
  EXPECT_EQ(b.y, 52);
  EXPECT_EQ(b.flags, EDGE_TOP | EDGE_BOTTOM);
  EXPECT_EQ(moved.layout->screen(1).shapeCount, 1);
  // A box taller than the band splits by lines, its cut edges open.
  Laid split("= T\n! " + words(8 * 70) + "\n");
  ASSERT_GE(split.screens, 3);
  ASSERT_EQ(split.layout->shapeCount(), split.screens);
  EXPECT_EQ(split.layout->shape(0).flags, EDGE_TOP);
  for (int i = 1; i + 1 < split.layout->shapeCount(); i++) EXPECT_EQ(split.layout->shape(i).flags, 0);
  EXPECT_EQ(split.layout->shape(split.layout->shapeCount() - 1).flags, EDGE_BOTTOM);
  expectInside(split);
}

TEST(GuideLayout, Figures) {
  MapSizer sizer;
  sizer.sizes["wide"] = {880, 400};
  sizer.sizes["small"] = {200, 100};
  sizer.sizes["tall"] = {440, 2000};
  {
    Laid l("= T\n@fig wide cap\ntext\n", &sizer);
    const Shape& f = l.layout->shape(0);
    EXPECT_EQ(f.kind, ShapeKind::Figure);
    EXPECT_STREQ(f.ref, "wide");
    EXPECT_EQ(f.x, 20);
    EXPECT_EQ(f.y, 92);
    EXPECT_EQ(f.w, 440);
    EXPECT_EQ(f.h, 200);
    EXPECT_EQ(l.runText(1), "text");
    EXPECT_EQ(l.run(1).y, 92 + 200 + 12);
  }
  {
    Laid l("= T\n@fig small\n", &sizer);
    EXPECT_EQ(l.layout->shape(0).x, 20 + (440 - 200) / 2);
    EXPECT_EQ(l.layout->shape(0).w, 200);  // never scaled up
  }
  {
    Laid l("= T\n@fig tall\n", &sizer);
    EXPECT_EQ(l.layout->shape(0).h, MAX_FIG_H);
    EXPECT_EQ(l.layout->shape(0).w, 106);
  }
  {
    Laid l("= T\n@fig gone\n", &sizer);
    EXPECT_EQ(l.layout->shape(0).flags, FIGURE_MISSING);
    EXPECT_EQ(l.layout->shape(0).h, 60);
    EXPECT_EQ(l.layout->shape(0).w, 440);
    Laid none("= T\n@fig gone\n", nullptr);
    EXPECT_EQ(none.layout->shape(0).flags, FIGURE_MISSING);
  }
  int w, h;
  fitFigure(1000, 500, 440, 480, w, h);
  EXPECT_EQ(w, 440);
  EXPECT_EQ(h, 220);
  fitFigure(0, 5, 440, 480, w, h);
  EXPECT_EQ(w, 0);
}

TEST(GuideLayout, LeadLineAboveTheTitle) {
  Laid l("= T\ntext\n", nullptr, "Reference only");
  EXPECT_EQ(l.runText(0), "Reference");
  EXPECT_EQ(l.run(0).font, Font::Lead);
  EXPECT_EQ(l.run(0).y, 52);
  EXPECT_EQ(l.runText(2), "T");
  EXPECT_EQ(l.run(2).y, 52 + 16 + 10);
  Laid empty("= T\ntext\n", nullptr, "");
  EXPECT_EQ(empty.runText(0), "T");
}

TEST(GuideLayout, OverlongWordsBreakBetweenCharacters) {
  Laid l("= T\n" + std::string(100, 'x') + "\n");
  ASSERT_EQ(l.layout->runCount(), 4);
  EXPECT_EQ(l.run(1).len, 44);
  EXPECT_EQ(l.run(2).len, 44);
  EXPECT_EQ(l.run(3).len, 12);
  EXPECT_EQ(l.run(3).y, 92 + 40);
  // Across a bold toggle, mid-word.
  Laid b("= T\n" + std::string(40, 'x') + "**" + std::string(10, 'y') + "**\n");
  ASSERT_EQ(b.layout->runCount(), 4);
  EXPECT_EQ(b.run(2).len, 4);
  EXPECT_EQ(b.run(2).font, Font::Bold);
  EXPECT_EQ(b.run(3).len, 6);
  EXPECT_EQ(b.run(3).font, Font::Bold);
  EXPECT_EQ(b.run(3).x, 20);
  expectInside(b);
}

TEST(GuideLayout, MarkersOnlyAndEmptyPagesLayOutCleanly) {
  Laid l("= T\n****\n");
  EXPECT_EQ(l.screens, 1);
  EXPECT_EQ(l.layout->runCount(), 1);
  Laid empty("= T\n");
  EXPECT_EQ(empty.screens, 1);
}

TEST(GuideLayout, ScreenCapTruncates) {
  Laid l("= T\n" + words(9 * 33 * 20) + "\n");  // ~20 screens of text
  EXPECT_EQ(l.screens, PageLayout::MAX_SCREENS);
  EXPECT_TRUE(l.layout->truncated());
  EXPECT_EQ(l.layout->screenCount(), PageLayout::MAX_SCREENS);
  const ScreenSpan& last = l.layout->screen(PageLayout::MAX_SCREENS - 1);
  EXPECT_LE(last.firstRun + last.runCount, l.layout->runCount());
  // Run storage fills first (1024 runs is ~3.5 screens of these words): later screens are empty.
  EXPECT_EQ(l.layout->runCount(), PageLayout::MAX_RUNS);
}

TEST(GuideLayout, InvalidPageAndTopicScreens) {
  Buf buf("= A\nx\n---\n= B\n" + words(9 * 40) + "\n");
  auto t = std::make_unique<TopicText>();
  ASSERT_EQ(t->parse(buf.data(), buf.len), GpError::None);
  const FakeMetrics m;
  auto pl = std::make_unique<PageLayout>();
  EXPECT_EQ(layoutPage(*t, 2, nullptr, m, testGeometry(), nullptr, pl.get()), 0);
  EXPECT_EQ(pl->screenCount(), 0);
  uint8_t per[4] = {};
  EXPECT_EQ(topicScreens(*t, nullptr, m, testGeometry(), nullptr, per, 4), 3);
  EXPECT_EQ(per[0], 1);
  EXPECT_EQ(per[1], 2);
}

TEST(GuideLayout, RandomPagesStayInsideAndCountsAgree) {
  std::mt19937 rng(7);
  const char* pieces[] = {"word", "**bold**", "x**y**", "a-b", "longerword", "**", "1.5", "(30 cm)"};
  for (int iter = 0; iter < 300; iter++) {
    std::string gp = "= Page " + std::to_string(iter) + "\n";
    const int blocks = static_cast<int>(rng() % 12);
    for (int b = 0; b < blocks; b++) {
      const char* prefix[] = {"", "- ", "1. ", "! ", "* "};
      gp += prefix[rng() % 5];
      const int n = 1 + static_cast<int>(rng() % 60);
      for (int k = 0; k < n; k++) gp += std::string(k ? " " : "") + pieces[rng() % 8];
      gp += "\n";
    }
    Laid l(gp);
    EXPECT_GE(l.screens, 1);
    expectInside(l);
  }
}

TEST(GuideLayout, CompactGeometryOnlyTightensGaps) {
  const Geometry d, c = compactGeometry();
  EXPECT_EQ(c.left, d.left);
  EXPECT_EQ(c.width, d.width);
  EXPECT_EQ(c.top, d.top);
  EXPECT_EQ(c.bottom, d.bottom);
  EXPECT_LT(c.blockGap, d.blockGap);
  EXPECT_LT(c.itemGap, d.itemGap);
  EXPECT_LT(c.boxPad, d.boxPad);
  // The default band sits between the breadcrumb strip and the 64 px bottom bar.
  EXPECT_GE(d.top, 40);
  EXPECT_LE(d.bottom, 800 - 64);
  EXPECT_EQ(d.left + d.width, 460);
}

TEST(GuideLayout, CopyRun) {
  TextRun r{"abcdef", 3, 0, 0, Font::Body};
  char buf[8];
  EXPECT_EQ(copyRun(r, buf, sizeof(buf)), 3u);
  EXPECT_STREQ(buf, "abc");
  EXPECT_EQ(copyRun(r, buf, 3), 2u);
  EXPECT_STREQ(buf, "ab");
}

TEST(GuideBreadcrumb, DropsLeadingPartsThenCuts) {
  const FakeMetrics m;
  const char* parts[] = {"SURVIVAL", "FIRE", "Fire lays"};
  char out[64];
  EXPECT_EQ(breadcrumb(parts, 3, m, Font::Crumb, 440, out, sizeof(out)), 27u);
  EXPECT_STREQ(out, "SURVIVAL / FIRE / FIRE LAYS");
  breadcrumb(parts, 3, m, Font::Crumb, 220, out, sizeof(out));
  EXPECT_STREQ(out, ".. / FIRE / FIRE LAYS");
  breadcrumb(parts, 3, m, Font::Crumb, 150, out, sizeof(out));
  EXPECT_STREQ(out, ".. / FIRE LAYS");
  breadcrumb(parts, 3, m, Font::Crumb, 80, out, sizeof(out));
  EXPECT_STREQ(out, "FIRE ...");
  breadcrumb(parts, 3, m, Font::Crumb, 20, out, sizeof(out));
  EXPECT_STREQ(out, "");
  breadcrumb(parts, 1, m, Font::Crumb, 440, out, sizeof(out));
  EXPECT_STREQ(out, "SURVIVAL");
  EXPECT_EQ(breadcrumb(parts, 0, m, Font::Crumb, 440, out, sizeof(out)), 0u);
  char tiny[10];
  breadcrumb(parts, 3, m, Font::Crumb, 440, tiny, sizeof(tiny));
  EXPECT_LT(std::strlen(tiny), sizeof(tiny));
}
