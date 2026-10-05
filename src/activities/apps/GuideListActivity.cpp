#include "GuideListActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "GuideStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {

using Button = MappedInputManager::Button;
using gd::lists::RowKind;
using gd::store::Store;

}  // namespace

GuideListActivity::GuideListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const gd::State& state,
                                     const bool fast)
    : GuideScreen(state.screen == gd::Screen::Home ? NAME_HOME : NAME_LIST, renderer, mappedInput, state) {
  halfPending = !fast;
}

gd::lists::Source GuideListActivity::source() const {
  Store& store = Store::get();
  gd::lists::Source s;
  if (store.isOpen()) {
    s.catalog = &store.catalog();
    s.info = &store.info();
    s.marks = &store.marks();
    s.recent = &store.recent();
  }
  s.home = home();
  s.list = st.list;
  s.category = st.category;
  s.query = st.query;
  s.hits = hits.get();
  s.hitCount = hitCount;
  return s;
}

void GuideListActivity::onEnter() {
  GuideScreen::onEnter();
  const auto& metrics = UITheme::getInstance().getMetrics();
  listTop = metrics.topPadding + metrics.headerHeight + 6;
  rowsPage = std::min(gd::draw::rowsPerPage(listTop), static_cast<int>(PAGE_MAX));
  packError = Store::get().open();
  if (packError == gd::PackError::None) {
    if (!home() && st.list == gd::ListKind::Search) runSearch();
    const int cap = gd::lists::capacity(Store::get().catalog());
    rows = makeUniqueNoThrow<gd::lists::Row[]>(static_cast<size_t>(cap));
    if (rows) {
      rowCount = gd::lists::build(source(), rows.get(), cap);
    } else {
      LOG_ERR("GD", "OOM: list rows");
    }
    restoreSelection();
  }
  publish();
  ready.store(true);
  requestUpdate();
}

void GuideListActivity::runSearch() {
  Store& store = Store::get();
  const gd::Catalog& c = store.catalog();
  hits = makeUniqueNoThrow<gd::Hit[]>(MAX_RESULTS);
  hitCount = 0;
  if (!hits) return;
  const unsigned long start = millis();
  gd::store::Text index;
  gd::store::Box<gd::SearchIndex> search;
  if (store.loadPackFile(gd::INDEX_FILE, index, gd::MAX_INDEX_BYTES) && search.make() &&
      search->attach(index.data(), index.size())) {
    hitCount = search->query(st.query, c, hits.get(), MAX_RESULTS);
  } else {
    // No index (or a damaged one): the titles and summaries still answer.
    LOG_ERR("GD", "search.idx unusable: title search only");
    uint16_t found[MAX_RESULTS];
    hitCount = c.titleSearch(st.query, found, MAX_RESULTS);
    for (int i = 0; i < hitCount; i++) hits[i] = gd::Hit{found[i], 0};
  }
  LOG_INF("GD", "Search \"%s\": %d in %lu ms", st.query, hitCount, millis() - start);
  // The index (~190 KB) goes with index / search here.
}

bool GuideListActivity::openable(const int index) const {
  return rows && index >= 0 && index < rowCount && rows[index].kind != RowKind::Note;
}

void GuideListActivity::restoreSelection() {
  selected = -1;
  if (st.topic[0]) {
    const int t = Store::get().catalog().findTopic(st.topic);
    for (int i = 0; i < rowCount && t >= 0; i++) {
      if (rows[i].kind == RowKind::Topic && rows[i].ref == t) {
        selected = i;
        break;
      }
    }
  }
  if (selected < 0 && openable(st.sel)) selected = st.sel;
  if (selected < 0 && openable(0)) selected = 0;
  top = selected > 0 ? (selected / rowsPage) * rowsPage : 0;
  syncState();
}

void GuideListActivity::syncState() {
  st.sel = static_cast<uint16_t>(selected > 0 ? selected : 0);
  st.topic[0] = '\0';
  if (openable(selected) && rows[selected].kind == RowKind::Topic) {
    gd::copyCut(Store::get().catalog().topic(rows[selected].ref).id, st.topic, sizeof(st.topic));
  }
}

void GuideListActivity::publish() {
  taskENTER_CRITICAL(&frameLock);
  shared.top = static_cast<int16_t>(top);
  shared.selected = static_cast<int16_t>(selected);
  shared.menu = menuState();
  taskEXIT_CRITICAL(&frameLock);
  requestUpdate();
}

void GuideListActivity::select(const int index) {
  if (rowCount == 0) return;
  const int i = std::max(0, std::min(index, rowCount - 1));
  if (!openable(i)) return;
  selected = i;
  top = (i / rowsPage) * rowsPage;
  syncState();
  noteChange();
  publish();
}

void GuideListActivity::turnPage(const int dir) {
  const int next = top + dir * rowsPage;
  if (next < 0 || next >= rowCount) return;
  top = next;
  // The selection comes along onto the new page.
  if (selected < top || selected >= top + rowsPage) {
    selected = openable(top) ? top : -1;
    syncState();
  }
  noteChange();
  publish();
}

void GuideListActivity::activate(const int index) {
  if (!openable(index)) return;
  selected = index;
  syncState();
  const gd::lists::Row& r = rows[index];
  const gd::Catalog& c = Store::get().catalog();
  gd::State s;
  s.screen = gd::Screen::List;
  switch (r.kind) {
    case RowKind::Category:
      s.list = gd::ListKind::Category;
      gd::copyCut(c.category(r.ref).id, s.category, sizeof(s.category));
      break;
    case RowKind::Topic:
      // The page keeps this list as its way back (and its NEXT walks through it).
      s = st;
      s.screen = gd::Screen::Page;
      gd::copyCut(c.topic(r.ref).id, s.topic, sizeof(s.topic));
      s.page = r.page;
      s.sub = 0;
      break;
    case RowKind::Quick:
      s.list = gd::ListKind::Quick;
      break;
    case RowKind::Search:
    case RowKind::NewSearch:
      startSearch();
      return;
    case RowKind::Marks:
      s.list = gd::ListKind::Marks;
      break;
    case RowKind::Recent:
      s.list = gd::ListKind::Recent;
      break;
    case RowKind::About:
      s = st;
      s.screen = gd::Screen::About;
      break;
    case RowKind::Note:
      return;
  }
  openScreen(s);
}

void GuideListActivity::goUp() {
  if (home() || packError != gd::PackError::None) {
    exitToApps();
    return;
  }
  // Back to the home with this list's row selected.
  gd::State s;
  s.screen = gd::Screen::Home;
  s.sel = static_cast<uint16_t>(gd::lists::homeRowOf(Store::get().catalog(), st.list, st.category));
  openScreen(s);
}

void GuideListActivity::loop() {
  if (!ready.load()) return;
  tickIdleSave(millis());
  if (packError == gd::PackError::None && handleMenuInput()) return;

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    switch (gd::draw::barButtonAt(x, y)) {
      case gd::draw::BarButton::Prev:
        turnPage(-1);
        return;
      case gd::draw::BarButton::Next:
        turnPage(1);
        return;
      case gd::draw::BarButton::Middle:
        goUp();
        return;
      case gd::draw::BarButton::None:
        break;
    }
    const int onPage = std::min(rowsPage, rowCount - top);
    const int row = gd::draw::rowAt(listTop, onPage, x, y);
    if (row >= 0 && openable(top + row)) {
      activate(top + row);
      return;
    }
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    turnPage(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    turnPage(-1);
    return;
  }
  // The header arrow, the left-edge swipe, or a Back key.
  if (mappedInput.wasReleased(Button::Back)) {
    goUp();
    return;
  }
  const Keys k = readKeys();
  if (k.upLong) {
    goUp();
    return;
  }
  if ((k.actLong || k.confirm) && selected >= 0) {
    activate(selected);
    return;
  }
  if (k.step != 0 && rowCount > 0) {
    int i = selected < 0 ? 0 : selected + k.step;
    // Skip a note row (nothing to open).
    while (i >= 0 && i < rowCount && !openable(i)) i += k.step;
    if (i >= 0 && i < rowCount) select(i);
  }
}

void GuideListActivity::render(RenderLock&&) {
  if (!ready.load()) return;
  Frame f;
  taskENTER_CRITICAL(&frameLock);
  f = shared;
  taskEXIT_CRITICAL(&frameLock);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const gd::lists::Source src = source();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 packError == gd::PackError::None ? gd::lists::title(src) : tr(STR_GUIDE));

  gd::draw::BarView bar;
  bar.middleLabel = tr(STR_GD_BACK);
  if (packError != gd::PackError::None) {
    const char* title = tr(STR_GD_NO_PACK);
    const char* paras[2] = {tr(STR_GD_NO_PACK_HOW), tr(STR_GD_NO_PACK_THEN)};
    if (packError == gd::PackError::Damaged) {
      title = tr(STR_GD_DAMAGED);
      paras[0] = tr(STR_GD_DAMAGED_HOW);
      paras[1] = "";
    } else if (packError == gd::PackError::NeedsNewerFirmware) {
      title = tr(STR_GD_NEWER);
      paras[0] = tr(STR_GD_NEWER_HOW);
      paras[1] = "";
    }
    gd::draw::drawMessage(renderer, listTop, title, paras, 2);
    gd::draw::drawBar(renderer, bar);
  } else {
    gd::draw::ListRow page[PAGE_MAX];
    const int onPage = std::max(0, std::min(rowsPage, rowCount - f.top));
    for (int i = 0; i < onPage; i++) {
      gd::lists::text(src, rows[f.top + i], page[i], values[i], sizeof(values[i]), subs[i], sizeof(subs[i]));
    }
    gd::draw::ListView view;
    view.listTop = listTop;
    view.rows = page;
    view.rowCount = onPage;
    view.selected = f.selected >= f.top && f.selected < f.top + onPage ? f.selected - f.top : -1;
    const int pageCount = (rowCount + rowsPage - 1) / rowsPage;
    char count[16] = "";
    if (pageCount > 1) std::snprintf(count, sizeof(count), "%d/%d", f.top / rowsPage + 1, pageCount);
    bar.middleDetail = count;
    bar.prevLabel = tr(STR_GD_PREV);
    bar.prevEnabled = f.top > 0;
    bar.nextLabel = tr(STR_GD_NEXT);
    bar.nextEnabled = f.top + rowsPage < rowCount;
    view.bar = bar;
    gd::draw::drawList(renderer, view);
    drawMenu(f.menu);
  }
  renderer.displayBuffer(pickRefresh(false, false));
}

#if CROSSPOINT_BENCH_CONSOLE
void GuideListActivity::benchInfo(BenchInfo& out) const {
  out = BenchInfo{};
  out.state = st;
  out.title = packError == gd::PackError::None ? gd::lists::title(source()) : tr(STR_GUIDE);
  out.rows = rowCount;
  out.selected = selected;
  out.menuOpen = menuState().open;
  if (packError == gd::PackError::Missing) out.message = "nopack";
  if (packError == gd::PackError::Damaged) out.message = "damaged";
  if (packError == gd::PackError::NeedsNewerFirmware) out.message = "newer";
}

bool GuideListActivity::benchRow(const int index, char* out, const size_t cap) const {
  if (!rows || index < 0 || index >= rowCount) return false;
  const gd::lists::Source src = source();
  const gd::lists::Row& r = rows[index];
  const char* ref = "-";
  if (r.kind == RowKind::Category) ref = src.catalog->category(r.ref).id;
  if (r.kind == RowKind::Topic) ref = src.catalog->topic(r.ref).id;
  gd::draw::ListRow text;
  char value[24];
  char sub[160];
  gd::lists::text(src, r, text, value, sizeof(value), sub, sizeof(sub));
  std::snprintf(out, cap, "%s %s | %s | %s | %s", gd::lists::kindName(r.kind), ref, text.title, text.value,
                text.subtitle);
  return true;
}

void GuideListActivity::benchStep(const int dir) {
  if (rowCount > 0) select(selected < 0 ? 0 : selected + dir);
}

void GuideListActivity::benchAct() {
  if (selected >= 0) activate(selected);
}

void GuideListActivity::benchUp() { goUp(); }

bool GuideListActivity::benchOpenRow(const int row) {
  if (!openable(row)) return false;
  if (rows[row].kind == RowKind::Search || rows[row].kind == RowKind::NewSearch) return false;  // a keyboard
  activate(row);
  return true;
}
#endif
