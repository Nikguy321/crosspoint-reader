#pragma once

#include <atomic>

#include "GuideApp.h"
#include "GuideDraw.h"
#include "GuideStore.h"

// A topic's pages (or About & sources: about.txt, the same format). The breadcrumb strip, the
// page's screen (GdLayout's display list: title, figure, steps, bullets, boxed WARNING / NOTE), and
// the bar: < PREV | MENU n/m | NEXT >. An authored page that does not fit continues on follow-on
// screens (n/m counts screens across the topic). NEXT on the topic's last screen opens the next
// topic of the list it came from (its category, or the quick cards; the bar names it), PREV on the
// first opens the previous topic's last screen; both are a replace with a FAST first frame. MENU (or
// Back: the left-edge swipe; or the left key held) goes up to that list with this topic selected.
// A medical topic shows the pack's reference-only line above its first page's title. Quick cards
// use the compact style (UI 10 text, tighter gaps) so a card is one screen.
//
// Touch: the bar; a swipe left / right turns; a tap on the figure opens it full screen (a tap
// returns); a finger held on the page toggles its bookmark. X4 Pro keys: right / left turn, the right
// key held toggles the bookmark ("Bookmarked" under MENU, a ribbon in the breadcrumb).
//
// The topic's text and every page's screen count are made in onEnter (the count under the render
// lock: it measures with the fonts); loop() only moves through them, and render() lays out the page
// it is asked for (fonts are the render task's).
class GuidePageActivity final : public GuideScreen {
 public:
  GuidePageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const gd::State& state, bool fast);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

#if CROSSPOINT_BENCH_CONSOLE
  void benchInfo(BenchInfo& out) const override;
  void benchStep(int dir) override;
  void benchAct() override;
  void benchUp() override;
  // Opens the full-screen figure (true) or closes it.
  void benchFigure(bool open);
#endif

 private:
  enum class Toast : uint8_t { None, Marked, Removed };
  struct Frame {
    uint8_t page = 0;
    uint8_t sub = 0;
    bool fullFigure = false;
    bool marked = false;
    bool halfNow = false;  // leaving the full-screen figure
    Toast toast = Toast::None;
    MenuState menu;
    gd::draw::BarView bar;
    char count[16] = {};  // "2/5"
  };
  // Where the figure on the screen drawn last is (render -> loop, under frameLock).
  struct FigureHit {
    uint8_t page = 0xFF;
    uint8_t sub = 0;
    int16_t x = 0, y = 0, w = 0, h = 0;
  };

  bool about() const { return st.screen == gd::Screen::About; }
  bool load();
  // The topic before (dir -1) or after (dir +1) in the list this one came from, or -1.
  int neighbour(int dir) const;
  gd::Step step(int dir, gd::Pos& out) const;
  void turn(int dir);
  void toggleMark();
  void goUp();
  void setFullFigure(bool open);
  void publish() override;
  const gd::FontSet& fonts() const;
  gd::Geometry geometry() const;
  const char* lead(int page) const;
  static bool drawFigure(void* ctx, GfxRenderer& r, const char* name, int x, int y, int w, int h);

  int topic = -1;  // catalog index (-1: About)
  int pages = 0;
  bool compact = false;
  bool failed = false;
  gd::PackError packError = gd::PackError::None;
  uint8_t perPage[gd::MAX_PAGES] = {};
  int total = 0;
  gd::store::Text file;
  gd::store::Box<gd::TopicText> text;
  gd::store::CardSizer sizer;
  gd::store::CardSizer sizerXL;  // the full-screen view's fig/XL rasters (render task only)
  bool fullFigure = false;
  Toast toast = Toast::None;
  bool halfNext = false;
  Frame shared;   // loop -> render, under frameLock
  FigureHit hit;  // render -> loop, under frameLock
  // render task only
  gd::store::Box<gd::PageLayout> layout;
  int laidPage = -1;
  char crumb[96] = {};
};
