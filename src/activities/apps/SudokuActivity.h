#pragma once

#include <Sudoku.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <memory>

#include "SudokuDraw.h"
#include "activities/Activity.h"

// Sudoku (Apps, touch boards only). The pure game lives in lib/Sudoku (engine, play rules, undo,
// saves, layout, touch), the drawing in SudokuDraw; this is the input, refresh, menu and save glue.
//
// Touch: tap a square, then a digit key (Notes on: the digit toggles a pencil mark); a digit or
// Erase held 600 ms, or tapped with no square selected, locks it, so each square tapped then gets
// it (the locked key tapped again unlocks). Notes / Erase / Undo / Menu below the digits. A
// contact counts only when it starts and ends on the same target. X4 Pro keys: the left key undoes
// (held: the menu, opened on release), the right key toggles Notes (held: a hint; on a solved
// puzzle, the next one); a long press of Home opens the menu. The header arrow and the left-edge
// swipe go back to the Apps list.
//
// A new puzzle is generated (numbered: the seed is fnv1a("sudoku <tier> <n>")) before any frame is
// requested, under a HalPowerManager::Lock (the full clock). loop() never takes the render lock for
// input: the game and the status note are handed to render() under a spinlock (render copies them
// first), so a tap during a refresh is never lost; frames coalesce. FAST per change; HALF on entry,
// a new puzzle, the return from the menu, completion and a forced refresh, and (once the reader's
// counter runs out) only at a pause: 2 s idle, Notes toggled, a key locked or unlocked. The game is
// saved at the 2 s pause, on the menu, on completion and on exit (every sleep is a reboot);
// sleeping here records the app in CrossPointState::lastSleepApp so every wake and boot reopens it.
class SudokuActivity final : public Activity {
 public:
  static constexpr uint8_t APP_ID = 3;  // CrossPointState::lastSleepApp
  static constexpr const char* NAME = "Sudoku";

  SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Full-rate input sampling (and no sleep) only while a finger is down.
  bool preventAutoSleep() override { return contact.active; }
  bool handleForcedRefresh() override;
  uint8_t resumeApp() const override { return APP_ID; }

#if CROSSPOINT_BENCH_CONSOLE
  // Bench console (SU): read and driven on the loop task, the game's only writer.
  const sd::Model* benchModel() const { return model.get(); }
  const sd::Prefs& benchPrefs() const { return prefs; }
  uint32_t benchElapsedSeconds() const { return elapsedMs / 1000; }
  // A puzzle generated now from a seed (the prefs' counters are left alone).
  bool benchStart(int tier, uint32_t number, uint32_t seed);
  // Cells 0..80, digits 1..9, through the model as the touch handlers use it.
  void benchPut(int cell, int digit);
  void benchNote(int cell, int digit);
  void benchErase(int cell);
  void benchHint();
  void benchCheck(sd::Scope scope);
  void benchReveal(sd::Scope scope);
  // Writes every missing or wrong digit (completion tests).
  void benchSolve();
#endif

 private:
  // What render() draws, handed over under frameLock.
  struct Frame {
    sd::Game game;
    sd::draw::Note note;
    bool pause = false;  // a pause point since the last frame (idle, Notes, a lock)
    bool half = false;   // this state owes a HALF (a new puzzle, completion)
  };

  // Generates and plays a puzzle; false when the generator gave none (it always gives one).
  bool startPuzzle(int tier, uint32_t number, uint32_t seed);
  // The next numbered puzzle of the prefs' tier.
  bool newPuzzle();
  void save();

  // cell: the square an edit wrote (its clash, if any, is named on the status line); -1 none.
  void apply(const sd::Change& change, int cell = -1);
  void publish(bool pause, bool half = false);
  // The banner's New puzzle may act: the banner is on the panel and (for a contact, its down time;
  // 0 for a key) the press began after it was shown.
  bool bannerArmed(uint32_t contactDownMs) const;
  void onSolved();
  void openMenu();
  void exitToApps();
  void handleTap(const sd::Target& target, uint32_t now);
  void handleHold(const sd::Target& target, uint32_t now);
  void handleKeys(uint32_t now);
  void tickElapsed(unsigned long now);
  void checkIdle(unsigned long now);

  std::unique_ptr<sd::Model> model;  // loop only (~4.6 KB: the game and the undo ring)
  std::unique_ptr<Frame> shared;     // loop -> render, under frameLock
  std::unique_ptr<Frame> drawn;      // render task only
  portMUX_TYPE frameLock = portMUX_INITIALIZER_UNLOCKED;
  sd::Prefs prefs;
  sd::Contact contact;
  sd::draw::Note note;  // the status line's message until the next change
  bool failed = false;  // out of memory: a message and Back
  // onEnter is done. A render already owed when the replace released the lock runs before onEnter;
  // it draws nothing rather than a half-loaded puzzle.
  std::atomic<bool> ready{false};
  // A tap ended without its release edge (a second finger): Back stays unread up to and
  // including that release.
  bool releasePending = false;
  bool menuOnRelease = false;  // left key held past the long press: open the menu on release
  bool dirty = false;          // edited since the last save
  uint32_t elapsedMs = 0;
  unsigned long lastTickMs = 0;
  unsigned long lastInputMs = 0;
  bool idleDone = false;  // this pause's save and HALF are done
  std::atomic<bool> halfPending{false};
  // millis() when a solved frame first reached the panel (0: not shown); written by render.
  std::atomic<uint32_t> bannerShownMs{0};
  std::atomic<int> rendersUntilHalf{0};  // written by the render task
  // render task only, off its stack
  char statusText[96] = {};
  char bannerTitle[48] = {};
  char bannerDetail[72] = {};
};
