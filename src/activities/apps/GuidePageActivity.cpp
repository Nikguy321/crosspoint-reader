#include "GuidePageActivity.h"

#include <Arduino.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

using Button = MappedInputManager::Button;
using gd::store::Store;

constexpr size_t PATH_CAP = 96;

}  // namespace

GuidePageActivity::GuidePageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const gd::State& state,
                                     const bool fast)
    : GuideScreen(NAME_PAGE, renderer, mappedInput, state) {
  halfPending = !fast;
}

const gd::FontSet& GuidePageActivity::fonts() const { return compact ? gd::compactFonts() : gd::pageFonts(); }

gd::Geometry GuidePageActivity::geometry() const { return compact ? gd::compactGeometry() : gd::Geometry{}; }

const char* GuidePageActivity::lead(const int page) const {
  if (about() || topic < 0 || page != 0) return nullptr;
  const Store& store = Store::get();
  return store.catalog().topic(topic).medical() ? store.info().refNote : nullptr;
}

void GuidePageActivity::onEnter() {
  GuideScreen::onEnter();
  Store& store = Store::get();
  packError = store.open();
  failed = packError != gd::PackError::None || !load();
  if (failed) LOG_ERR("GD", "Page %s not shown", about() ? "about" : st.topic);
  publish();
  ready.store(true);
  requestUpdate();
}

bool GuidePageActivity::load() {
  Store& store = Store::get();
  const gd::Catalog& c = store.catalog();
  const char* fileName = gd::ABOUT_FILE;
  size_t cap = gd::MAX_ABOUT_BYTES;
  if (!about()) {
    topic = c.findTopic(st.topic);
    if (topic < 0) return false;
    compact = c.topic(topic).quick();
    fileName = c.topic(topic).file;
    cap = gd::MAX_GP_BYTES;
  }
  if (!store.loadPackFile(fileName, file, cap) || !text.make()) return false;
  const gd::GpError err = text->parse(file.data(), file.size());
  if (err != gd::GpError::None) {
    LOG_ERR("GD", "%s refused (%d, line %d)", fileName, static_cast<int>(err), text->errorLine());
    return false;
  }
  pages = text->pageCount();
  sizer.setPack(store.packId());
  sizerXL.setPack(store.packId(), gd::FIGURE_XL_DIR);
  {
    // Every page's screen count (the bar's n/m and where NEXT goes) measures with the fonts: the
    // render task's, so under its lock.
    RenderLock lock(*this);
    const gd::RendererMetrics metrics(renderer, fonts());
    total = gd::topicScreens(*text, lead(0), metrics, geometry(), &sizer, perPage, gd::MAX_PAGES);
  }
  if (st.page >= pages) st.page = static_cast<uint8_t>(pages - 1);
  st.sub = gd::resolveSub(st.sub, perPage[st.page]);
  if (!about()) {
    store.touchRecent(c.topic(topic).id);  // written with the state (GuideScreen::saveState)
  }
  return true;
}

int GuidePageActivity::neighbour(const int dir) const {
  if (about() || topic < 0) return -1;
  const gd::Catalog& c = Store::get().catalog();
  if (st.list == gd::ListKind::Quick) {
    for (int i = topic + dir; i >= 0 && i < c.topicCount(); i += dir) {
      if (c.topic(i).quick()) return i;
    }
    return -1;
  }
  const gd::Category& k = c.category(c.topic(topic).category);
  const int n = topic + dir;
  return n >= k.first && n < k.first + k.count ? n : -1;
}

gd::Step GuidePageActivity::step(const int dir, gd::Pos& out) const {
  if (failed || pages <= 0) return gd::Step::None;
  // The sequence NEXT / PREV walk: this topic and its neighbours in the list it came from.
  uint16_t seqTopics[3];
  int n = 0;
  const int self = about() ? 0 : topic;
  const int before = neighbour(-1);
  const int after = neighbour(1);
  if (before >= 0) seqTopics[n++] = static_cast<uint16_t>(before);
  seqTopics[n++] = static_cast<uint16_t>(self);
  if (after >= 0) seqTopics[n++] = static_cast<uint16_t>(after);
  gd::Seq seq;
  seq.topics = seqTopics;
  seq.count = n;
  gd::Pos at;
  at.topic = static_cast<int16_t>(self);
  at.page = st.page;
  at.sub = st.sub;
  const gd::Catalog& c = Store::get().catalog();
  return dir > 0 ? gd::nextPos(at, perPage[st.page], pages, seq, c, out)
                 : gd::prevPos(at, perPage[st.page], pages, seq, c, out);
}

void GuidePageActivity::turn(const int dir) {
  gd::Pos to;
  const gd::Step s = step(dir, to);
  if (s == gd::Step::None) return;
  if (s == gd::Step::OtherTopic) {
    const gd::Catalog& c = Store::get().catalog();
    gd::State next = st;
    gd::copyCut(c.topic(to.topic).id, next.topic, sizeof(next.topic));
    next.page = to.page;
    next.sub = to.sub;
    // Out of a search, the bookmarks or the recent list, the walk is the topic's category.
    if (next.list != gd::ListKind::Quick) {
      next.list = gd::ListKind::Category;
      gd::copyCut(c.category(c.topic(to.topic).category).id, next.category, sizeof(next.category));
    }
    openScreen(next, /*fast=*/true);
    return;
  }
  st.page = to.page;
  st.sub = gd::resolveSub(to.sub, perPage[to.page]);
  toast = Toast::None;
  noteChange();
  publish();
}

void GuidePageActivity::toggleMark() {
  if (about() || topic < 0 || failed) return;
  Store& store = Store::get();
  const bool marked = store.marks().toggle(store.catalog().topic(topic).id, st.page);
  if (!store.saveMarks()) LOG_ERR("GD", "Bookmarks not saved");
  toast = marked ? Toast::Marked : Toast::Removed;
  publish();
}

void GuidePageActivity::goUp() {
  gd::State s = st;
  s.page = 0;
  s.sub = 0;
  s.screen = gd::Screen::List;
  if (about() && st.list == gd::ListKind::None) {
    s.screen = gd::Screen::Home;
  } else if (s.list == gd::ListKind::None && topic >= 0) {
    const gd::Catalog& c = Store::get().catalog();
    s.list = gd::ListKind::Category;
    gd::copyCut(c.category(c.topic(topic).category).id, s.category, sizeof(s.category));
  }
  if (!openScreen(s)) exitToApps();
}

void GuidePageActivity::setFullFigure(const bool open) {
  if (open == fullFigure) return;
  if (open && (failed || !text->page(st.page).figure)) return;
  fullFigure = open;
  if (!open) halfNext = true;  // the page comes back clean
  publish();
}

void GuidePageActivity::publish() {
  Frame f;
  f.page = st.page;
  f.sub = st.sub;
  f.fullFigure = fullFigure;
  f.halfNow = halfNext;
  halfNext = false;
  f.toast = toast;
  f.menu = menuState();
  if (!failed && !about() && topic >= 0) {
    Store& store = Store::get();
    f.marked = store.marks().has(store.catalog().topic(topic).id, st.page);
  }
  if (!failed) {
    const gd::Catalog& c = Store::get().catalog();
    gd::Pos to;
    const gd::Step back = step(-1, to);
    f.bar.prevLabel = tr(STR_GD_PREV);
    f.bar.prevEnabled = back != gd::Step::None;
    if (back == gd::Step::OtherTopic) f.bar.prevDetail = c.topic(to.topic).title;
    const gd::Step fwd = step(1, to);
    f.bar.nextLabel = tr(STR_GD_NEXT);
    f.bar.nextEnabled = fwd != gd::Step::None;
    if (fwd == gd::Step::OtherTopic) f.bar.nextDetail = c.topic(to.topic).title;
    gd::Pos at;
    at.topic = static_cast<int16_t>(topic);
    at.page = st.page;
    at.sub = st.sub;
    std::snprintf(f.count, sizeof(f.count), "%d/%d", gd::screenOrdinal(at, perPage, pages), total);
  }
  f.bar.middleLabel = failed ? tr(STR_GD_BACK) : tr(STR_GD_MENU);
  taskENTER_CRITICAL(&frameLock);
  shared = f;
  taskEXIT_CRITICAL(&frameLock);
  requestUpdate();
}

void GuidePageActivity::loop() {
  if (!ready.load()) return;
  tickIdleSave(millis());
  if (failed) {
    int x = 0;
    int y = 0;
    const bool tapUp = mappedInput.wasScreenTapped(x, y) && gd::draw::barButtonAt(x, y) == gd::draw::BarButton::Middle;
    if (tapUp || mappedInput.wasReleased(Button::Back)) goUp();
    return;
  }
  if (handleMenuInput()) return;
  int x = 0;
  int y = 0;

  if (fullFigure) {
    const Keys k = readKeys();
    if (mappedInput.wasScreenTapped(x, y) || mappedInput.wasReleased(Button::Back) || k.step != 0 || k.upLong ||
        k.confirm) {
      setFullFigure(false);
    }
    return;
  }

  // A finger held on the page: the bookmark.
  if (mappedInput.wasScreenLongPress(x, y)) {
    if (y >= gd::draw::CRUMB_H && y < gd::draw::BAR_TOP) toggleMark();
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    switch (gd::draw::barButtonAt(x, y)) {
      case gd::draw::BarButton::Prev:
        turn(-1);
        return;
      case gd::draw::BarButton::Middle:
        goUp();
        return;
      case gd::draw::BarButton::Next:
        turn(1);
        return;
      case gd::draw::BarButton::None:
        break;
    }
    FigureHit h;
    taskENTER_CRITICAL(&frameLock);
    h = hit;
    taskEXIT_CRITICAL(&frameLock);
    if (h.page == st.page && h.sub == st.sub && x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
      setFullFigure(true);
    }
    return;
  }
  // The header arrow does not exist here: Back is the left-edge swipe or a Back key.
  if (mappedInput.wasReleased(Button::Back)) {
    goUp();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    turn(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right && !mappedInput.wasBackGesture()) {
    turn(-1);
    return;
  }
  const Keys k = readKeys();
  if (k.actLong) {
    toggleMark();
  } else if (k.upLong || k.confirm) {
    goUp();
  } else if (k.step != 0) {
    turn(k.step);
  }
}

bool GuidePageActivity::drawFigure(void* ctx, GfxRenderer& r, const char* name, const int x, const int y, const int w,
                                   const int h) {
  // ctx: the figure folder (gd::FIGURE_DIR or gd::FIGURE_XL_DIR).
  const char* dir = ctx ? static_cast<const char*>(ctx) : gd::FIGURE_DIR;
  char path[PATH_CAP];
  if (gd::figurePath(Store::get().packId(), name, path, sizeof(path), dir) == 0) return false;
  RenderConfig config{x, y, w, h};
  config.useGrayscale = false;
  config.useDithering = false;
  config.useExactDimensions = true;
  PngToFramebufferConverter png;
  const unsigned long start = millis();
  const bool ok = png.decodeToFramebuffer(path, r, config);
  LOG_DBG("GD", "Figure %s %dx%d in %lu ms%s", name, w, h, millis() - start, ok ? "" : " (failed)");
  return ok;
}

void GuidePageActivity::render(RenderLock&&) {
  if (!ready.load()) return;
  Frame f;
  taskENTER_CRITICAL(&frameLock);
  f = shared;
  shared.halfNow = false;
  taskEXIT_CRITICAL(&frameLock);
  const unsigned long start = millis();
  renderer.clearScreen();

  if (failed) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   tr(STR_GUIDE));
    const char* paras[1] = {tr(STR_GD_DAMAGED_HOW)};
    gd::draw::drawMessage(renderer, metrics.topPadding + metrics.headerHeight + 6, tr(STR_GD_DAMAGED), paras, 1);
    gd::draw::drawBar(renderer, f.bar);
    renderer.displayBuffer(pickRefresh(true, false));
    return;
  }

  bool figureShown = false;
  if (f.fullFigure) {
    const gd::PageText& pg = text->page(f.page);
    gd::draw::FigureView v;
    v.name = pg.figure ? pg.figure : "";
    // The pack's full-screen raster when it has one (larger, and turned sideways when the figure is
    // wide), else the page's figure.
    int pageW = 0;
    int pageH = 0;
    const bool havePage = pg.figure && sizer.size(pg.figure, pageW, pageH);
    const char* dir = gd::FIGURE_DIR;
    if (pg.figure && sizerXL.size(pg.figure, v.figW, v.figH)) {
      dir = gd::FIGURE_XL_DIR;
    } else if (havePage) {
      v.figW = pageW;
      v.figH = pageH;
    } else {
      v.figW = v.figH = 0;
    }
    const bool turned = dir == gd::FIGURE_XL_DIR && havePage && (pageW > pageH) != (v.figW > v.figH);
    v.caption = pg.caption ? pg.caption : "";
    v.hint = turned ? tr(STR_GD_TURN_TAP_TO_RETURN) : tr(STR_GD_TAP_TO_RETURN);
    v.missingLabel = tr(STR_GD_FIGURE_MISSING);
    v.figure = &GuidePageActivity::drawFigure;
    v.figureCtx = const_cast<char*>(dir);
    gd::draw::drawFigureScreen(renderer, v);
    figureShown = true;
  } else {
    if (!layout && !layout.make()) {
      LOG_ERR("GD", "OOM: page layout");
      renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_MEMORY_ERROR));
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      return;
    }
    const gd::RendererMetrics metrics(renderer, fonts());
    if (laidPage != f.page) {
      gd::layoutPage(*text, f.page, lead(f.page), metrics, geometry(), &sizer, layout.get());
      laidPage = f.page;
    }
    const int sub = gd::resolveSub(f.sub, layout->screenCount());

    const Store& store = Store::get();
    const char* parts[3] = {store.info().shortTitle, "", ""};
    int count = 2;
    if (about()) {
      parts[1] = tr(STR_GD_ABOUT);
    } else {
      const gd::Topic& t = store.catalog().topic(topic);
      parts[1] = store.catalog().category(t.category).title;
      parts[2] = t.title;
      count = 3;
    }
    gd::breadcrumb(parts, count, metrics, gd::Font::Crumb, gd::draw::crumbWidth(), crumb, sizeof(crumb));
    gd::draw::drawCrumb(renderer, crumb, f.marked);

    gd::draw::PageView pv;
    pv.layout = layout.get();
    pv.screen = sub;
    pv.fonts = &fonts();
    pv.figure = &GuidePageActivity::drawFigure;
    pv.figureCtx = const_cast<char*>(gd::FIGURE_DIR);
    pv.missingLabel = tr(STR_GD_FIGURE_MISSING);
    const int fig = gd::draw::drawPageBody(renderer, pv);
    FigureHit h;
    h.page = f.page;
    h.sub = static_cast<uint8_t>(sub);
    if (fig >= 0) {
      const gd::Shape& s = layout->shape(fig);
      h.x = s.x;
      h.y = s.y;
      h.w = s.w;
      h.h = s.h;
      figureShown = true;
    }
    taskENTER_CRITICAL(&frameLock);
    hit = h;
    taskEXIT_CRITICAL(&frameLock);

    gd::draw::BarView bar = f.bar;
    if (f.toast != Toast::None) {
      bar.middleDetail = f.toast == Toast::Marked ? tr(STR_GD_BOOKMARKED) : tr(STR_GD_REMOVED);
      bar.middleDetailBold = true;
    } else {
      bar.middleDetail = f.count;
    }
    gd::draw::drawBar(renderer, bar);
  }
  drawMenu(f.menu);
  const auto mode = pickRefresh(f.halfNow, figureShown);
  LOG_DBG("GD", "Page drawn in %lu ms (%s)", millis() - start, mode == HalDisplay::HALF_REFRESH ? "half" : "fast");
  renderer.displayBuffer(mode);
}

#if CROSSPOINT_BENCH_CONSOLE
void GuidePageActivity::benchInfo(BenchInfo& out) const {
  out = BenchInfo{};
  out.state = st;
  out.menuOpen = menuState().open;
  out.fullFigure = fullFigure;
  out.compact = compact;
  if (failed) {
    out.message = packError == gd::PackError::None ? "damaged" : "nopack";
    return;
  }
  const gd::PageText& pg = text->page(st.page);
  out.title = pg.title;
  out.figure = pg.figure ? pg.figure : "";
  out.pages = pages;
  out.screens = perPage[st.page];
  out.total = total;
  gd::Pos at;
  at.topic = static_cast<int16_t>(topic);
  at.page = st.page;
  at.sub = st.sub;
  out.ordinal = gd::screenOrdinal(at, perPage, pages);
  if (!about() && topic >= 0) {
    Store& store = Store::get();
    out.marked = store.marks().has(store.catalog().topic(topic).id, st.page);
  }
}

void GuidePageActivity::benchStep(const int dir) {
  if (!failed) turn(dir);
}

void GuidePageActivity::benchAct() { toggleMark(); }

void GuidePageActivity::benchUp() { goUp(); }

void GuidePageActivity::benchFigure(const bool open) { setFullFigure(open); }
#endif
