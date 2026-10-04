#pragma once

#include <Crossword.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <memory>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Crossword (Apps, touch boards only). The pure game lives in lib/Crossword, the drawing in
// CrosswordDraw; this is the input, refresh, menu and save glue.
//
// Touch: a tap on a square moves the cursor there (a tap on the cursor turns Across / Down), on
// the clue text turns too, on "<" / ">" goes to the previous / next clue; the keyboard types
// (holding Del clears the word; Menu opens the menu). A contact counts only when it starts and
// ends on the same target. X4 Pro keys: a short press of the right / left key goes to the next /
// previous clue, a long press of the right key turns Across / Down (on a solved puzzle: the next
// puzzle), of the left key opens the clue list; a long press of Home opens the menu. The header
// arrow and the left-edge swipe go back to the Apps list.
//
// loop() never takes the render lock for input: the fill, flags, cursor and the bar message are
// handed to render() under a spinlock (render copies them first), so a key pressed while a
// frame is on its way to the panel is never lost; frames coalesce. Progress is saved at every
// word boundary, check, reveal, solve and on exit; sleeping here records the app in
// CrossPointState::lastSleepApp so every wake and boot path reopens it.
class CrosswordActivity final : public Activity {
 public:
  static constexpr uint8_t APP_ID = 2;  // CrossPointState::lastSleepApp
  static constexpr const char* NAME = "Crossword";

  CrosswordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Full-rate input sampling (and no sleep) only while a finger is down.
  bool preventAutoSleep() override { return contact.active; }
  bool handleForcedRefresh() override;
  uint8_t resumeApp() const override { return APP_ID; }

#if CROSSPOINT_BENCH_CONSOLE
  // Bench console (CW): read and driven on the loop task, the game's only writer.
  const cw::Puzzle* benchPuzzle() const { return puzzle.get(); }
  const cw::Progress* benchProgress() const { return prog.get(); }
  const cw::Prefs& benchPrefs() const { return prefs; }
  uint32_t benchElapsedSeconds() const { return elapsedMs / 1000; }
  // Opens a puzzle by source key; the status says why one was refused.
  cw::LoadStatus benchOpen(const char* sourceKey);
  // Letters A-Z through the same handler as the keyboard's keys; '-' is Del.
  void benchType(const char* keys);
  // The cursor on a white square; dir -1 keeps the direction. False on a block or off the grid.
  bool benchCursor(int row, int col, int dir);
  void benchCheck(cw::Scope scope);
  void benchReveal(cw::Scope scope);
  // Types every missing or wrong answer letter through the keyboard's handler (completion tests).
  void benchSolve();
#endif

 private:
  enum class Bar : uint8_t { Clue, NotQuite, AllSolved };

  // What render() draws, handed over under frameLock.
  struct Frame {
    cw::Progress prog;
    Bar bar = Bar::Clue;
    uint16_t wrong = 0;    // for Bar::NotQuite
    bool hasNext = false;  // the banner's Next puzzle (else "All solved here")
    bool pause = false;    // a pause point since the last frame (another word, or idle)
    bool half = false;     // this state owes a HALF (a new puzzle, completion)
  };

  // Loads a source into a new puzzle and plays it; the status says why one was refused.
  cw::LoadStatus openSource(const char* sourceKey);
  // Plays a parsed puzzle: its saved progress (or fresh), the current one in the prefs.
  void adopt(std::unique_ptr<cw::Puzzle> fresh);
  // The next unsolved puzzle of the current one's collection (built-ins: the first unsolved one
  // when there is no current one). open: open it (skipping files that are refused). False when
  // every one is solved.
  bool nextUnsolved(bool open);
  bool openFirstBuiltin();
  void saveProgress();

  void handleTap(const cw::Target& target);
  void pressKey(int keyIndex);
  void apply(const cw::Change& change);
  void publish(bool pause, bool half = false);
  // The banner's Next puzzle may act: the banner is on the panel and (for a contact, its down
  // time; 0 for a key) the press began after it was shown.
  bool bannerArmed(uint32_t contactDownMs) const;
  void showBar(Bar bar);
  void onSolved();
  void openMenu();
  void openClueList();
  void openPicker();
  void exitToApps();
  void handleKeys();
  void tickElapsed(unsigned long now);
  void checkIdleRefresh(unsigned long now);

  std::unique_ptr<cw::Puzzle> puzzle;  // replaced only under the render lock
  std::unique_ptr<cw::Progress> prog;  // loop only
  std::unique_ptr<Frame> shared;       // loop -> render, under frameLock
  std::unique_ptr<Frame> drawn;        // render task only
  std::unique_ptr<cw::Puzzle> staged;  // the picker's choice, until adopted
  portMUX_TYPE frameLock = portMUX_INITIALIZER_UNLOCKED;
  cw::Prefs prefs;
  cw::ScreenLayout layout;  // set with the puzzle, under the render lock
  cw::Contact contact;
  Bar bar = Bar::Clue;
  uint16_t barWrong = 0;
  bool hasNext = false;
  bool failed = false;  // out of memory or no puzzle at all: a message and Back
  // onEnter is done. A render already owed when the replace released the lock runs before onEnter;
  // it draws nothing rather than a half-loaded puzzle.
  std::atomic<bool> ready{false};
  // A tap ended without its release edge (a second finger): Back stays unread up to and
  // including that release.
  bool releasePending = false;
  bool clueListOnRelease = false;  // left key held past the long press: open the list on release
  uint32_t elapsedMs = 0;
  unsigned long lastTickMs = 0;
  unsigned long lastInputMs = 0;
  bool idleHalfAsked = false;
  std::atomic<bool> halfPending{false};
  // millis() when a solved frame first reached the panel (0: not shown); written by render.
  std::atomic<uint32_t> bannerShownMs{0};
  std::atomic<int> rendersUntilHalf{0};  // written by the render task
  // render task only, off its stack
  char clueLabel[8] = {};
  char barText[64] = {};
  char bannerTitle[48] = {};
  char bannerDetail[64] = {};
  ButtonNavigator buttonNavigator;
};
