// The survival guide on the host with the real renderer and fonts (SleepCardHost) and the real device
// pack (packs/guide/build/survival): writes the screen set to build/guide/*.png for a look, and checks
// that nothing below the header is drawn in the bezel columns (x 0..7, 472..479, where the left-edge
// Back swipe starts), that a page's text and boxes stay in the text column (x 20..459) and the
// content band (y 44..733), that the figure is drawn where the layout put it, and that the bar's
// labels stay in their thirds.
//
// The list screens' headers are stand-ins (title, back arrow and underline where Classic and Lyra
// draw them): GUI.drawHeader is device-only. Everything else is the device's drawing
// (src/activities/apps/GuideDraw) over the device's rows (GuideListModel) and layout (lib/Guide).
// The figures are decoded here with zlib; the device draws them with PNGdec.
#include <I18n.h>
#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "CardPreview.h"
#include "GuideTestUtil.h"
#include "src/activities/apps/GuideDraw.h"
#include "src/activities/apps/GuideListModel.h"
#include "src/activities/apps/GuideMetrics.h"
#include "src/fontIds.h"

using namespace gd;
using guidetest::Buf;
using guidetest::Pack;
using guidetest::PackSizer;
using guidetest::readFile;
using sleepcards::preview::renderer;

namespace {

enum class Theme { Classic, Lyra };

int listTopFor(const Theme t) { return (t == Theme::Classic ? 5 : 10) + 84 + 6; }

std::string outDir() {
  const std::string dir = std::string(CARD_PREVIEW_REPO_ROOT) + "/build/guide";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

void save(const std::string& name) {
  ASSERT_TRUE(sleepcards::preview::writeFramePng(outDir() + "/" + name + ".png")) << name;
}

// Stand-ins for GUI.drawHeader: Classic's band is y 5..89, Lyra's 10..94.
void stubHeader(GfxRenderer& r, const Theme theme, const char* title) {
  const int top = theme == Theme::Classic ? 5 : 10;
  r.drawText(UI_12_FONT_ID, 18, top + 36, "<", true, EpdFontFamily::BOLD);
  r.drawText(UI_12_FONT_ID, theme == Theme::Classic ? 60 : 56, top + 36, title, true, EpdFontFamily::BOLD);
  r.fillRect(0, top + 81, 480, theme == Theme::Classic ? 2 : 3, true);
}

const Pack& pack() {
  static Pack p(GUIDE_PACK);
  return p;
}

// ---- the pack's 1-bit PNGs, decoded for the preview --------------------------------------------------

struct Png {
  int w = 0;
  int h = 0;
  std::vector<uint8_t> ink;  // w * h, 1 = black
};

uint32_t be32(const uint8_t* p) {
  return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}

// A non-interlaced 1-bit grayscale PNG (what make_pack.py writes); false for anything else.
bool decodePng(const std::string& path, Png& out) {
  const std::string bytes = readFile(path);
  const auto* d = reinterpret_cast<const uint8_t*>(bytes.data());
  if (bytes.size() < 33 || std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) != 0) return false;
  std::vector<uint8_t> idat;
  size_t at = 8;
  int depth = 0;
  int type = -1;
  while (at + 12 <= bytes.size()) {
    const uint32_t len = be32(d + at);
    const char* kind = reinterpret_cast<const char*>(d + at + 4);
    if (at + 12 + len > bytes.size()) return false;
    const uint8_t* body = d + at + 8;
    if (std::memcmp(kind, "IHDR", 4) == 0) {
      out.w = static_cast<int>(be32(body));
      out.h = static_cast<int>(be32(body + 4));
      depth = body[8];
      type = body[9];
      if (body[12] != 0) return false;  // interlaced
    } else if (std::memcmp(kind, "IDAT", 4) == 0) {
      idat.insert(idat.end(), body, body + len);
    }
    at += 12 + len;
  }
  if (depth != 1 || type != 0 || out.w <= 0 || out.h <= 0) return false;
  const size_t stride = (static_cast<size_t>(out.w) + 7) / 8;
  std::vector<uint8_t> raw((stride + 1) * out.h);
  uLongf rawLen = raw.size();
  if (uncompress(raw.data(), &rawLen, idat.data(), idat.size()) != Z_OK || rawLen != raw.size()) return false;
  std::vector<uint8_t> prev(stride, 0);
  std::vector<uint8_t> line(stride);
  out.ink.assign(static_cast<size_t>(out.w) * out.h, 0);
  for (int y = 0; y < out.h; y++) {
    const uint8_t* src = raw.data() + y * (stride + 1);
    const uint8_t filter = src[0];
    for (size_t i = 0; i < stride; i++) {
      const uint8_t a = i > 0 ? line[i - 1] : 0;
      const uint8_t b = prev[i];
      const uint8_t c = i > 0 ? prev[i - 1] : 0;
      uint8_t v = src[1 + i];
      switch (filter) {
        case 1:
          v = static_cast<uint8_t>(v + a);
          break;
        case 2:
          v = static_cast<uint8_t>(v + b);
          break;
        case 3:
          v = static_cast<uint8_t>(v + (a + b) / 2);
          break;
        case 4: {
          const int p = a + b - c;
          const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
          v = static_cast<uint8_t>(v + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c));
          break;
        }
        default:
          break;
      }
      line[i] = v;
    }
    for (int x = 0; x < out.w; x++) out.ink[y * out.w + x] = ((line[x / 8] >> (7 - x % 8)) & 1) == 0;
    prev = line;
  }
  return true;
}

int figuresDrawn = 0;

// GuideDraw's FigureFn on the host: nearest-neighbour into w x h, as the device's decoder scales.
// ctx: the figure folder ("fig/L" when null), as on the device.
bool hostFigure(void* ctx, GfxRenderer& r, const char* name, const int x, const int y, const int w, const int h) {
  Png png;
  const char* dir = ctx ? static_cast<const char*>(ctx) : FIGURE_DIR;
  if (!decodePng(std::string(GUIDE_PACK) + "/" + dir + "/" + name + ".png", png)) return false;
  for (int dy = 0; dy < h; dy++) {
    const int sy = dy * png.h / h;
    for (int dx = 0; dx < w; dx++) {
      if (png.ink[sy * png.w + dx * png.w / w]) r.drawPixel(x + dx, y + dy, true);
    }
  }
  figuresDrawn++;
  return true;
}

// ---- checks ------------------------------------------------------------------------------------------

struct Ink {
  int x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
  bool any() const { return x1 >= 0; }
};
Ink inkIn(const int left, const int top, const int right, const int bottom) {
  Ink ink;
  for (int y = top; y <= bottom; y++) {
    for (int x = left; x <= right; x++) {
      if (!renderer().readPixel(x, y)) continue;
      ink.x0 = std::min(ink.x0, x);
      ink.y0 = std::min(ink.y0, y);
      ink.x1 = std::max(ink.x1, x);
      ink.y1 = std::max(ink.y1, y);
    }
  }
  return ink;
}

void expectBezelClear(const int fromY, const std::string& what) {
  EXPECT_FALSE(inkIn(0, fromY, draw::BEZEL - 1, draw::SCREEN_H - 1).any()) << what;
  EXPECT_FALSE(inkIn(draw::SCREEN_W - draw::BEZEL, fromY, draw::SCREEN_W - 1, draw::SCREEN_H - 1).any()) << what;
}

// The page's band: everything inside the text column (the bar and the crumb are outside it).
void expectBandInColumn(const std::string& what) {
  const Geometry g;
  EXPECT_FALSE(inkIn(draw::BEZEL, g.top, g.left - 1, g.bottom - 1).any()) << what;
  EXPECT_FALSE(inkIn(g.left + g.width, g.top, draw::SCREEN_W - draw::BEZEL - 1, g.bottom - 1).any()) << what;
  EXPECT_FALSE(inkIn(0, g.bottom, draw::SCREEN_W - 1, draw::BAR_TOP - 1).any()) << what << ": below the band";
}

// The bar's labels stay inside their thirds (a few px clear of each divider).
void expectBarThirds(const std::string& what) {
  for (const int divider : {draw::BAR_THIRD, 2 * draw::BAR_THIRD}) {
    EXPECT_FALSE(inkIn(divider - 4, draw::BAR_TOP + 4, divider - 1, draw::SCREEN_H - 1).any()) << what;
    EXPECT_FALSE(inkIn(divider + 1, draw::BAR_TOP + 4, divider + 4, draw::SCREEN_H - 1).any()) << what;
  }
}

// ---- the screens, drawn as the activities draw them -----------------------------------------------------

const RendererMetrics& metricsFor(const bool compact) {
  static RendererMetrics page(renderer(), pageFonts());
  static RendererMetrics small(renderer(), compactFonts());
  return compact ? small : page;
}

struct ListScreen {
  lists::Source src;
  Marks marks;
  Recent recent;
  std::vector<Hit> hits;
  std::string query;
  std::string category;
  int selected = 0;
  int top = 0;
};

void drawListScreen(ListScreen& s, const Theme theme, const std::string& name) {
  GfxRenderer& r = renderer();
  const Pack& p = pack();
  s.src.catalog = p.catalog.get();
  s.src.info = &p.info;
  s.src.marks = &s.marks;
  s.src.recent = &s.recent;
  s.src.category = s.category.c_str();
  s.src.query = s.query.c_str();
  s.src.hits = s.hits.data();
  s.src.hitCount = static_cast<int>(s.hits.size());
  std::vector<lists::Row> rows(lists::capacity(*p.catalog));
  const int count = lists::build(s.src, rows.data(), static_cast<int>(rows.size()));
  const int listTop = listTopFor(theme);
  const int perPage = draw::rowsPerPage(listTop);
  r.clearScreen();
  stubHeader(r, theme, lists::title(s.src));
  draw::ListRow page[16];
  char values[16][24];
  char subs[16][160];
  const int onPage = std::min(perPage, count - s.top);
  for (int i = 0; i < onPage; i++) lists::text(s.src, rows[s.top + i], page[i], values[i], 24, subs[i], 160);
  draw::ListView view;
  view.listTop = listTop;
  view.rows = page;
  view.rowCount = onPage;
  view.selected = s.selected - s.top;
  const int pages = (count + perPage - 1) / perPage;
  char countText[16] = "";
  if (pages > 1) std::snprintf(countText, sizeof(countText), "%d/%d", s.top / perPage + 1, pages);
  view.bar.middleLabel = tr(STR_GD_BACK);
  view.bar.middleDetail = countText;
  view.bar.prevLabel = tr(STR_GD_PREV);
  view.bar.prevEnabled = s.top > 0;
  view.bar.nextLabel = tr(STR_GD_NEXT);
  view.bar.nextEnabled = s.top + perPage < count;
  draw::drawList(r, view);
  expectBezelClear(listTop, name);
  expectBarThirds(name);
  save(name);
}

// A page screen as GuidePageActivity draws it. Returns the figure shape index drawn (-1 none).
struct PageShot {
  int screens = 0;
  int figure = -1;
  Shape figureShape;
};

PageShot drawPageScreen(const int topic, const int pageIndex, const int sub, const std::string& name,
                        const bool marked = false, const bool menu = false, const char* toast = nullptr) {
  GfxRenderer& r = renderer();
  const Pack& p = pack();
  const Topic& t = p.catalog->topic(topic);
  Buf buf;
  auto text = std::make_unique<TopicText>();
  EXPECT_EQ(p.loadTopic(topic, buf, *text), GpError::None);
  const bool compact = t.quick();
  const RendererMetrics& m = metricsFor(compact);
  const Geometry g = compact ? compactGeometry() : Geometry{};
  const PackSizer sizer(GUIDE_PACK);
  const char* lead = t.medical() ? p.info.refNote : nullptr;
  uint8_t perPage[MAX_PAGES];
  const int total = topicScreens(*text, lead, m, g, &sizer, perPage, MAX_PAGES);
  auto layout = std::make_unique<PageLayout>();
  PageShot shot;
  shot.screens = layoutPage(*text, pageIndex, pageIndex == 0 ? lead : nullptr, m, g, &sizer, layout.get());

  r.clearScreen();
  char crumb[96];
  const char* parts[3] = {p.info.shortTitle, p.catalog->category(t.category).title, t.title};
  breadcrumb(parts, 3, m, Font::Crumb, draw::crumbWidth(), crumb, sizeof(crumb));
  draw::drawCrumb(r, crumb, marked);
  draw::PageView pv;
  pv.layout = layout.get();
  pv.screen = sub;
  pv.fonts = compact ? &compactFonts() : &pageFonts();
  pv.figure = &hostFigure;
  pv.missingLabel = tr(STR_GD_FIGURE_MISSING);
  shot.figure = draw::drawPageBody(r, pv);
  if (shot.figure >= 0) shot.figureShape = layout->shape(shot.figure);

  // The bar, as GuidePageActivity::publish() makes it (the walk is the topic's category).
  const Category& cat = p.catalog->category(t.category);
  uint16_t seqTopics[3];
  int n = 0;
  if (topic > cat.first) seqTopics[n++] = static_cast<uint16_t>(topic - 1);
  seqTopics[n++] = static_cast<uint16_t>(topic);
  if (topic + 1 < cat.first + cat.count) seqTopics[n++] = static_cast<uint16_t>(topic + 1);
  Seq seq;
  seq.topics = seqTopics;
  seq.count = n;
  Pos at;
  at.topic = static_cast<int16_t>(topic);
  at.page = static_cast<uint8_t>(pageIndex);
  at.sub = static_cast<uint8_t>(sub);
  Pos to;
  draw::BarView bar;
  bar.prevLabel = tr(STR_GD_PREV);
  const Step back = prevPos(at, perPage[pageIndex], text->pageCount(), seq, *p.catalog, to);
  bar.prevEnabled = back != Step::None;
  if (back == Step::OtherTopic) bar.prevDetail = p.catalog->topic(to.topic).title;
  bar.nextLabel = tr(STR_GD_NEXT);
  const Step fwd = nextPos(at, perPage[pageIndex], text->pageCount(), seq, *p.catalog, to);
  bar.nextEnabled = fwd != Step::None;
  if (fwd == Step::OtherTopic) bar.nextDetail = p.catalog->topic(to.topic).title;
  char count[16];
  std::snprintf(count, sizeof(count), "%d/%d", screenOrdinal(at, perPage, text->pageCount()), total);
  bar.middleLabel = tr(STR_GD_MENU);
  bar.middleDetail = toast ? toast : count;
  bar.middleDetailBold = toast != nullptr;
  draw::drawBar(r, bar);

  if (!menu) {
    expectBandInColumn(name);
    expectBezelClear(draw::CRUMB_H, name);
    expectBarThirds(name);
  } else {
    draw::MenuView mv;
    mv.title = tr(STR_GD_MENU_TITLE);
    mv.labels[0] = tr(STR_GD_SEARCH);
    mv.labels[1] = tr(STR_GD_BOOKMARKS);
    mv.labels[2] = tr(STR_GD_RECENT);
    mv.labels[3] = tr(STR_GD_QUICK_CARDS);
    mv.labels[4] = tr(STR_GD_ABOUT);
    mv.count = 5;
    mv.selected = 0;
    draw::drawMenu(r, mv);
  }
  save(name);
  return shot;
}

int topicIndex(const char* id) {
  const int i = pack().catalog->findTopic(id);
  EXPECT_GE(i, 0) << id;
  return i;
}

}  // namespace

TEST(GuidePreview, HomeBothThemes) {
  ASSERT_EQ(pack().catalogError, PackError::None) << "run scripts/guide/make_pack.py";
  for (const Theme theme : {Theme::Lyra, Theme::Classic}) {
    ListScreen s;
    s.src.home = true;
    s.marks.toggle("bleeding-and-shock", 2);
    s.recent.touch("fire-lays");
    s.selected = 1;
    drawListScreen(s, theme, theme == Theme::Lyra ? "home" : "home_classic");
    // EMERGENCY is the first row, inverted: its row is mostly ink.
    const int top = listTopFor(theme);
    const Ink row = inkIn(draw::ROW_LEFT, top, draw::ROW_RIGHT - 1, top + draw::ROW_H - 1);
    EXPECT_LE(row.y0, top + 4);
    int black = 0;
    // Below the subtitle, inside the black fill.
    for (int x = 40; x < 440; x += 4) black += renderer().readPixel(x, top + draw::ROW_H - 7) ? 1 : 0;
    EXPECT_GT(black, 90) << "the EMERGENCY row is not inverted";
  }
  // The second home page (the rest of the categories and About).
  ListScreen s;
  s.src.home = true;
  const int perPage = draw::rowsPerPage(listTopFor(Theme::Lyra));
  s.top = perPage;
  s.selected = perPage;
  drawListScreen(s, Theme::Lyra, "home_page2");
}

TEST(GuidePreview, EmergencyTopicList) {
  ListScreen s;
  s.src.list = ListKind::Category;
  s.category = pack().catalog->category(0).id;
  EXPECT_EQ(s.category, "emergency");
  s.selected = 0;
  drawListScreen(s, Theme::Lyra, "list_emergency");
}

TEST(GuidePreview, QuickCardsAndBookmarksAndRecent) {
  {
    ListScreen s;
    s.src.list = ListKind::Quick;
    drawListScreen(s, Theme::Lyra, "list_quick");
  }
  {
    ListScreen s;
    s.src.list = ListKind::Marks;
    s.marks.toggle("fire-lays", 1);
    s.marks.toggle("bleeding-and-shock", 2);
    drawListScreen(s, Theme::Lyra, "list_bookmarks");
  }
  {
    ListScreen s;
    s.src.list = ListKind::Marks;  // none yet: the note row
    s.selected = -1;
    drawListScreen(s, Theme::Lyra, "list_bookmarks_empty");
  }
}

TEST(GuidePreview, SearchResults) {
  const std::string idx = readFile(std::string(GUIDE_PACK) + "/search.idx");
  auto index = std::make_unique<SearchIndex>();
  ASSERT_TRUE(index->attach(idx.data(), idx.size()));
  for (const char* q : {"bleeding", "zzqx"}) {
    ListScreen s;
    s.src.list = ListKind::Search;
    s.query = q;
    Hit hits[60];
    const int n = index->query(q, *pack().catalog, hits, 60);
    s.hits.assign(hits, hits + n);
    s.selected = n > 0 ? 1 : 0;
    drawListScreen(s, Theme::Lyra, std::string("search_") + q);
    if (std::string(q) == "bleeding") {
      ASSERT_GT(n, 0);
      EXPECT_STREQ(pack().catalog->topic(hits[0].topic).id, "bleeding-and-shock");
    } else {
      EXPECT_EQ(n, 0);
    }
  }
}

TEST(GuidePreview, FigurePage) {
  figuresDrawn = 0;
  const PageShot shot = drawPageScreen(topicIndex("fire-lays"), 0, 0, "page_figure");
  ASSERT_GE(shot.figure, 0);
  EXPECT_EQ(figuresDrawn, 1);
  // The figure is ink inside its rectangle, centred in the column.
  const Shape& f = shot.figureShape;
  const Ink ink = inkIn(f.x, f.y, f.x + f.w - 1, f.y + f.h - 1);
  EXPECT_TRUE(ink.any());
  EXPECT_NEAR(f.x + f.w / 2, 240, 1);
}

TEST(GuidePreview, StepsWithWarningPage) {
  // The first page in the pack with numbered steps and a WARNING box.
  const Pack& p = pack();
  for (int i = 0; i < p.catalog->topicCount(); i++) {
    Buf buf;
    auto text = std::make_unique<TopicText>();
    ASSERT_EQ(p.loadTopic(i, buf, *text), GpError::None);
    for (int pg = 0; pg < text->pageCount(); pg++) {
      const PageText& page = text->page(pg);
      bool steps = false;
      bool warning = false;
      for (int b = page.firstBlock; b < page.firstBlock + page.blockCount; b++) {
        steps |= text->block(b).kind == BlockKind::Step;
        warning |= text->block(b).kind == BlockKind::Warning;
      }
      if (!steps || !warning || page.figure) continue;
      std::printf("[guide] steps + WARNING: %s page %d\n", p.catalog->topic(i).id, pg + 1);
      drawPageScreen(i, pg, 0, "page_steps_warning");
      return;
    }
  }
  FAIL() << "no page with steps and a WARNING";
}

TEST(GuidePreview, MedicalLeadBookmarkAndFollowOn) {
  // A medical topic's first page: the reference-only line over the title; bookmarked, with the toast.
  drawPageScreen(topicIndex("bleeding-and-shock"), 0, 0, "page_medical", /*marked=*/true, false, tr(STR_GD_BOOKMARKED));
  // A page that needs follow-on screens: its last screen.
  const int t = topicIndex("water-needs");
  const PageShot first = drawPageScreen(t, 1, 0, "page_followon_1");
  ASSERT_GE(first.screens, 2);
  drawPageScreen(t, 1, first.screens - 1, "page_followon_last");
}

TEST(GuidePreview, QuickCardCompact) {
  const PageShot shot = drawPageScreen(topicIndex("signal-now"), 0, 0, "quick_card");
  EXPECT_EQ(shot.screens, 1);
  // The longest card (medical: the reference line above the title, a NOTE box at the end).
  const PageShot longest = drawPageScreen(topicIndex("head-spine"), 0, 0, "quick_card_head_spine");
  EXPECT_EQ(longest.screens, 1);
}

TEST(GuidePreview, GuideMenu) {
  drawPageScreen(topicIndex("fire-lays"), 1, 0, "menu", false, /*menu=*/true);
  // The menu's rows answer taps where they are drawn.
  const int top = draw::menuTop(5);
  EXPECT_EQ(draw::menuRowAt(5, 240, top + 52 + 10), 0);
  EXPECT_EQ(draw::menuRowAt(5, 240, top + 52 + 4 * draw::MENU_ROW_H + 10), 4);
  EXPECT_EQ(draw::menuRowAt(5, 20, 400), -2);
}

// The full-screen view draws the pack's fig/XL raster 1:1: larger than the page's figure (a wide one
// turned sideways), inside the box, clear of the caption and the bezel.
void fullScreenFigure(const char* name, const char* caption, const bool expectTurned, const std::string& shot) {
  GfxRenderer& r = renderer();
  r.clearScreen();
  draw::FigureView v;
  v.name = name;
  const PackSizer page(GUIDE_PACK);
  const PackSizer full(GUIDE_PACK, FIGURE_XL_DIR);
  int pw = 0, ph = 0;
  ASSERT_TRUE(page.size(name, pw, ph));
  ASSERT_TRUE(full.size(name, v.figW, v.figH)) << name << ": no full-screen raster";
  const bool turned = (pw > ph) != (v.figW > v.figH);
  EXPECT_EQ(turned, expectTurned) << name;
  v.caption = caption;
  v.hint = turned ? tr(STR_GD_TURN_TAP_TO_RETURN) : tr(STR_GD_TAP_TO_RETURN);
  v.missingLabel = tr(STR_GD_FIGURE_MISSING);
  v.figure = &hostFigure;
  v.figureCtx = const_cast<char*>(FIGURE_XL_DIR);
  const int before = figuresDrawn;
  draw::drawFigureScreen(r, v);
  EXPECT_EQ(figuresDrawn, before + 1) << name;
  expectBezelClear(0, shot);
  // The drawing's ink: inside the box, and larger than the page's along the drawing's long axis.
  const Ink ink =
      inkIn(draw::BEZEL, 0, draw::SCREEN_W - draw::BEZEL - 1, draw::FIGURE_BOX_TOP + draw::FIGURE_BOX_H - 1);
  ASSERT_TRUE(ink.any()) << name;
  EXPECT_GE(ink.x0, (draw::SCREEN_W - draw::FIGURE_BOX_W) / 2) << name;
  EXPECT_LE(ink.x1, (draw::SCREEN_W + draw::FIGURE_BOX_W) / 2 - 1) << name;
  EXPECT_GE(ink.y0, draw::FIGURE_BOX_TOP) << name;
  const int pageLong = std::max(pw, ph);
  const int fullLong = std::max(ink.x1 - ink.x0, ink.y1 - ink.y0) + 1;
  EXPECT_GE(fullLong * 100, pageLong * 114) << name << ": the full-screen view must be larger than the page";
  // The caption sits below the box, above the hint's rule.
  EXPECT_FALSE(inkIn(draw::BEZEL, draw::FIGURE_BOX_TOP + draw::FIGURE_BOX_H, draw::SCREEN_W - draw::BEZEL - 1,
                     draw::FIGURE_BOX_TOP + draw::FIGURE_BOX_H + 11)
                   .any())
      << name << ": a gap between the drawing and its caption";
  save(shot);
}

TEST(GuidePreview, FullScreenFigure) {
  // Fire lays: a wide figure, turned sideways.
  fullScreenFigure("atp-p125-1", "Fire lays: tepee, lean-to, cross-ditch, pyramid", true, "figure_full");
}

TEST(GuidePreview, FullScreenFigureUpright) {
  // A figure about as tall as wide stays upright, just larger.
  fullScreenFigure("atp-p110-2", "Basket, pool and tidal fish traps", false, "figure_full_upright");
}

TEST(GuidePreview, AboutPages) {
  GfxRenderer& r = renderer();
  const Pack& p = pack();
  Buf buf(readFile(std::string(GUIDE_PACK) + "/about.txt"));
  auto text = std::make_unique<TopicText>();
  ASSERT_EQ(text->parse(buf.data(), buf.len), GpError::None);
  auto layout = std::make_unique<PageLayout>();
  const RendererMetrics& m = metricsFor(false);
  uint8_t perPage[MAX_PAGES];
  const int total = topicScreens(*text, nullptr, m, Geometry{}, nullptr, perPage, MAX_PAGES);
  for (int pg = 0; pg < text->pageCount(); pg++) {
    const int screens = layoutPage(*text, pg, nullptr, m, Geometry{}, nullptr, layout.get());
    ASSERT_GE(screens, 1);
    r.clearScreen();
    char crumb[96];
    const char* parts[2] = {p.info.shortTitle, tr(STR_GD_ABOUT)};
    breadcrumb(parts, 2, m, Font::Crumb, draw::crumbWidth(), crumb, sizeof(crumb));
    draw::drawCrumb(r, crumb, false);
    draw::PageView pv;
    pv.layout = layout.get();
    pv.fonts = &pageFonts();
    draw::drawPageBody(r, pv);
    draw::BarView bar;
    bar.prevLabel = tr(STR_GD_PREV);
    // As GuidePageActivity walks About: its own pages and screens, no neighbouring topic.
    const uint16_t self = 0;
    Seq seq;
    seq.topics = &self;
    seq.count = 1;
    Pos here;
    here.topic = 0;
    here.page = static_cast<uint8_t>(pg);
    Pos to;
    bar.prevEnabled = prevPos(here, screens, text->pageCount(), seq, *p.catalog, to) != Step::None;
    bar.nextLabel = tr(STR_GD_NEXT);
    bar.nextEnabled = nextPos(here, screens, text->pageCount(), seq, *p.catalog, to) != Step::None;
    bar.middleLabel = tr(STR_GD_MENU);
    Pos at;
    at.page = static_cast<uint8_t>(pg);
    char count[16];
    std::snprintf(count, sizeof(count), "%d/%d", screenOrdinal(at, perPage, text->pageCount()), total);
    bar.middleDetail = count;
    draw::drawBar(r, bar);
    const std::string name = "about_" + std::to_string(pg + 1);
    expectBandInColumn(name);
    expectBezelClear(draw::CRUMB_H, name);
    save(name);
  }
}

TEST(GuidePreview, NoPackPage) {
  GfxRenderer& r = renderer();
  for (const Theme theme : {Theme::Lyra, Theme::Classic}) {
    r.clearScreen();
    stubHeader(r, theme, tr(STR_GUIDE));
    const char* paras[2] = {tr(STR_GD_NO_PACK_HOW), tr(STR_GD_NO_PACK_THEN)};
    draw::drawMessage(r, listTopFor(theme), tr(STR_GD_NO_PACK), paras, 2);
    draw::BarView bar;
    bar.middleLabel = tr(STR_GD_BACK);
    draw::drawBar(r, bar);
    const std::string name = theme == Theme::Lyra ? "nopack" : "nopack_classic";
    expectBezelClear(listTopFor(theme), name);
    // Every word of the instructions is on the screen (nothing cut at the bar).
    EXPECT_FALSE(inkIn(0, draw::BAR_TOP - 8, draw::SCREEN_W - 1, draw::BAR_TOP - 1).any()) << name;
    save(name);
  }
}

TEST(GuidePreview, HitTargets) {
  EXPECT_EQ(draw::barButtonAt(10, draw::BAR_TOP + 5), draw::BarButton::Prev);
  EXPECT_EQ(draw::barButtonAt(240, draw::SCREEN_H - 1), draw::BarButton::Middle);
  EXPECT_EQ(draw::barButtonAt(470, draw::BAR_TOP), draw::BarButton::Next);
  EXPECT_EQ(draw::barButtonAt(240, draw::BAR_TOP - 1), draw::BarButton::None);
  const int top = listTopFor(Theme::Lyra);
  EXPECT_EQ(draw::rowAt(top, 5, 240, top), 0);
  EXPECT_EQ(draw::rowAt(top, 5, 240, top + 4 * draw::ROW_H + 1), 4);
  EXPECT_EQ(draw::rowAt(top, 5, 240, top + 5 * draw::ROW_H + 1), -1);
  EXPECT_EQ(draw::rowAt(top, 5, 3, top + 10), -1);  // the bezel is the Back swipe's
  // A page of rows ends above the bar.
  EXPECT_LE(top + draw::rowsPerPage(top) * draw::ROW_H, draw::BAR_TOP);
}
