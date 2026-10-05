#pragma once

#include <Guide.h>
#include <HalDisplay.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <cstddef>
#include <memory>

#include "activities/Activity.h"

// Survival guide (Apps, touch boards only): the screens' shared plumbing. The pure half is lib/Guide
// (pack, pages, layout, navigation, search, state codecs); the card is GuideStore; the drawing is
// GuideDraw. Each screen is its own activity, opened with a REPLACE from a gd::State (what state.txt
// says), so no stack has to survive sleep and the way back is always rebuilt from the state:
//   GuideListActivity  the guide home (GuideHome) and the topic lists (GuideList): a category, the
//                      quick cards, search results, bookmarks, recent
//   GuidePageActivity  a topic's pages (and follow-on screens), the full-screen figure, About
// Every screen resumes after sleep (CrossPointState::lastSleepApp 4: main.cpp's resumeLastApp ->
// ActivityManager::goToGuide(true) -> the saved state). The search keyboard is the only pushed
// screen; sleeping inside it resumes the screen beneath it.
//
// Holding Home opens the guide menu (Search, Bookmarks, Recent, Quick cards, About & sources) over
// any guide screen. loop() never takes the render lock to record input: what render() draws is handed
// over in a small frame under a spinlock, and the content a screen draws (rows, a topic's text) is
// built before its first frame and not changed after. Refresh: FAST per change, HALF on entering a
// screen (but not a page turn into the next topic), leaving the full-screen figure, the reader's
// refresh counter, and every FIGURE_HALF_EVERY-th figure screen.
class GuideScreen : public Activity {
 public:
  static constexpr uint8_t APP_ID = 4;  // CrossPointState::lastSleepApp
  static constexpr const char* NAME_HOME = "GuideHome";
  static constexpr const char* NAME_LIST = "GuideList";
  static constexpr const char* NAME_PAGE = "GuidePage";
  static constexpr int FIGURE_HALF_EVERY = 4;

  // The first screen: the guide home (the Apps row, APP guide), or the saved state (resume).
  static std::unique_ptr<Activity> makeEntry(GfxRenderer& renderer, MappedInputManager& mappedInput, bool resume);
  // Leaves the guide for the Apps list (its row selected).
  static void exitToApps();

  // Replaces this screen with the one the state names (sanitized against the pack). fast: the first
  // frame may be FAST (a page turn into another topic). False when it could not be allocated.
  bool openScreen(const gd::State& state, bool fast = false);

  void onEnter() override;
  void onExit() override;
  uint8_t resumeApp() const override { return APP_ID; }
  bool handleForcedRefresh() override;

#if CROSSPOINT_BENCH_CONSOLE
  // Bench console (GD): read and driven on the loop task.
  struct BenchInfo {
    gd::State state;           // the screen as state.txt says it
    const char* title = "";    // the list's header / the page's title
    int rows = 0;              // list rows
    int selected = -1;         // list selection
    int screens = 0;           // page: screens on this page
    int ordinal = 0;           // page: n of m across the topic
    int total = 0;             //
    int pages = 0;             // page: authored pages
    const char* figure = "";   // page: this page's figure ("" none)
    bool marked = false;       // page: bookmarked
    bool compact = false;      // page: the quick-card style
    bool fullFigure = false;   // page: the full-screen figure is up
    bool menuOpen = false;     // the guide menu is up
    const char* message = "";  // a screen showing an error (no pack, damaged, ...)
  };
  virtual void benchInfo(BenchInfo& out) const = 0;
  // A list row as "<ref> | <title> | <subtitle>"; false past the end.
  virtual bool benchRow(int index, char* out, size_t cap) const { return false; }
  // Next / previous (a page turn, or the list's selection), as the right / left key.
  virtual void benchStep(int dir) = 0;
  // Opens the selected row (lists) or toggles the page's bookmark (pages).
  virtual void benchAct() = 0;
  // Up a level (the page's MENU, the list's BACK).
  virtual void benchUp() = 0;
  // Opens list row n (as a tap); false when there is no such row.
  virtual bool benchOpenRow(int row) { return false; }
#endif

 protected:
  GuideScreen(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput, const gd::State& state);

  // ---- the guide menu (Home held) ----
  enum MenuItem : uint8_t { MENU_SEARCH = 0, MENU_MARKS, MENU_RECENT, MENU_QUICK, MENU_ABOUT, MENU_COUNT };
  struct MenuState {
    bool open = false;
    int8_t selected = 0;
  };
  // Call first in loop(): true when the pass belonged to the menu (open, opened or closed).
  bool handleMenuInput();
  void openMenu();
  void closeMenu();
  // Draws the menu over the screen when the frame says it is open (render task).
  void drawMenu(const MenuState& menu) const;
  // The menu's state as loop() holds it: subclasses copy it into their frame in publish().
  const MenuState& menuState() const { return menu; }
  void runMenuItem(int item);
  // The search keyboard (pushed); a query opens the results.
  void startSearch();

  // The frame changed: copy what render() reads under frameLock, then requestUpdate().
  virtual void publish() = 0;

  // ---- refresh (render task) ----
  // halfNow: this frame owes a HALF (entry, a big change); figure: the screen shows a figure.
  HalDisplay::RefreshMode pickRefresh(bool halfNow, bool figure);

  // Keys on the X4 Pro's edges: a long press acts on its threshold (and swallows its release), a
  // left long press acts once the key is let go (opened at the threshold, the held key would reach
  // the next screen). Returns +1 / -1 for a short right / left press, 0 otherwise; sets the flags.
  struct Keys {
    int step = 0;          // +1 next, -1 previous
    bool actLong = false;  // right key held
    bool upLong = false;   // left key held and released
    bool confirm = false;  // front buttons: Confirm
  };
  Keys readKeys();

  // Saves st (the screen's state) when it changed since the last save (a new screen: always), and
  // the recent list when it changed.
  void saveState(bool force = false);
  // Something changed (st or the view): st is saved once input pauses (IDLE_SAVE_MS).
  void noteChange();
  void tickIdleSave(unsigned long now);

  gd::State st;  // this screen, loop task only
  portMUX_TYPE frameLock = portMUX_INITIALIZER_UNLOCKED;
  // onEnter is done. A render already owed when the replace released the lock runs before onEnter;
  // it draws nothing rather than a half-built screen.
  std::atomic<bool> ready{false};
  std::atomic<bool> halfPending{true};
  unsigned long lastChangeMs = 0;

 private:
  MenuState menu;
  bool upOnRelease = false;  // the left key passed its long press: act on release
  bool everSaved = false;
  gd::State saved;
};
