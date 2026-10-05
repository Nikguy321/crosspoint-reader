#pragma once

#include <memory>

#include "GuideApp.h"
#include "GuideDraw.h"
#include "GuideListModel.h"

// The guide home and the topic lists (one class; the state's screen and list kind decide, and
// GuideListModel says what is listed):
//   home (GuideHome)   EMERGENCY (inverted), Quick cards, Search, Bookmarks (n), Recent, the other
//                      categories, About & sources; with no pack (or a damaged / newer one) a page
//                      saying what to do instead
//   a list (GuideList) a category's topics, the quick cards, search results (New search first),
//                      bookmarks, recent
// Rows are paged: the bar's < PREV | BACK n/m | NEXT > turns the list's pages, so does a swipe up /
// down. Tap a row to open it. X4 Pro keys: right / left move the selection (the page follows), the
// right key held opens it, the left key held goes up a level (on release). Back (the header arrow,
// the left-edge swipe) goes up: a list to the home (its row selected), the home to Apps. The
// selection comes back to the topic the state names (MENU on a page selects its topic). A search
// runs as the screen opens: search.idx is read into PSRAM, queried and let go.
class GuideListActivity final : public GuideScreen {
 public:
  GuideListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const gd::State& state, bool fast);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

#if CROSSPOINT_BENCH_CONSOLE
  void benchInfo(BenchInfo& out) const override;
  bool benchRow(int index, char* out, size_t cap) const override;
  void benchStep(int dir) override;
  void benchAct() override;
  void benchUp() override;
  bool benchOpenRow(int row) override;
#endif

 private:
  static constexpr int MAX_RESULTS = 60;
  static constexpr int PAGE_MAX = 16;  // rows a page can hold at most (the scratch below)

  struct Frame {
    int16_t top = 0;  // the first row on the page shown
    int16_t selected = -1;
    MenuState menu;
  };

  bool home() const { return st.screen == gd::Screen::Home; }
  gd::lists::Source source() const;
  void runSearch();
  void restoreSelection();
  void publish() override;
  void select(int index);
  void turnPage(int dir);
  void activate(int index);
  void goUp();
  void syncState();
  bool openable(int index) const;

  int rowsPage = 1;  // rows per page (the theme header's height decides)
  int listTop = 96;
  std::unique_ptr<gd::lists::Row[]> rows;
  int rowCount = 0;
  int top = 0;
  int selected = -1;
  std::unique_ptr<gd::Hit[]> hits;  // search results (MAX_RESULTS)
  int hitCount = 0;
  gd::PackError packError = gd::PackError::None;
  Frame shared;  // loop -> render, under frameLock
  // render task scratch
  char values[PAGE_MAX][24] = {};
  char subs[PAGE_MAX][160] = {};
};
