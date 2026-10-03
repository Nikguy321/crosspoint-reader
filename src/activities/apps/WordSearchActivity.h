#pragma once

#include <WordSearch.h>

#include <atomic>
#include <memory>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Word Search (Apps). The pure game lives in lib/WordSearch, the drawing in WordSearchDraw;
// this is the input, refresh, menu and save glue.
//
// Touch: a drag from one letter to another selects the line between them; a tap anchors a
// letter and a tap on a letter in line with it selects the line (a second tap on the anchor
// clears it). The first key press only shows the cursor. X4 Pro keys: a short press of the
// right / left key moves the cursor to the next / previous letter, a long press of the right key
// taps at the cursor, of the left key clears the anchor; a long press of Home opens the menu.
// Front-button boards move the cursor with the arrows, tap with Confirm (hold it for the menu)
// and clear or leave with Back.
//
// The puzzle is saved on every found word and on exit; sleeping here records the app in
// CrossPointState::lastSleepApp so every wake and boot path reopens it.
class WordSearchActivity final : public Activity {
 public:
  static constexpr uint8_t APP_ID = 1;  // CrossPointState::lastSleepApp
  static constexpr const char* NAME = "WordSearch";

  // The first paint is always HALF (a loaded or new puzzle), which also cleans the sleep card a
  // wake leaves on the panel.
  WordSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Full-rate input sampling (and no sleep) only while a finger is down.
  bool preventAutoSleep() override { return contact.active; }
  bool handleForcedRefresh() override;
  uint8_t resumeApp() const override { return APP_ID; }

#if CROSSPOINT_BENCH_CONSOLE
  // Bench console (WS): read on the loop task, which is the only writer.
  const ws::Puzzle* benchPuzzle() const { return puzzle.get(); }
  ws::Cell benchAnchor() const { return anchor; }
  bool benchCursorShown() const { return showCursor.load(); }
  uint32_t benchElapsedSeconds() const { return elapsedMs / 1000; }
  const ws::Prefs& benchPrefs() const { return prefs; }
  // A deterministic new puzzle: difficulty -1 = the chosen one, themeKey nullptr = a built-in
  // theme picked with the seed alone (not the card's files, the choice or the recent ones).
  // False when that theme cannot make a puzzle.
  bool benchNewPuzzle(uint32_t seed, int difficulty, const char* themeKey);
#endif

 private:
  enum class Status : uint8_t { Progress, NoMatch, AlreadyFound, Hint };

  // Starts a puzzle now (seed, difficulty); themeKey nullptr = the player's choice. strict:
  // fail rather than fall back to another theme.
  bool startPuzzle(uint32_t seed, ws::Difficulty difficulty, const char* themeKey, bool strict);
  // Picks the theme for a new puzzle (the choice, or with `random` or no choice Random, which
  // avoids the last 3 themes; builtinsOnly: Random leaves out the card's files) into key.
  bool pickTheme(uint32_t& rng, bool random, bool builtinsOnly, char* key, size_t cap);
  // themeLoaded (optional): whether the theme itself could be read.
  bool generate(const char* key, uint32_t seed, ws::Difficulty difficulty, ws::Puzzle& out,
                bool* themeLoaded = nullptr);
  void installPuzzle(const ws::Puzzle& fresh);
  void savePuzzle();
  // True when it opened the menu or a new puzzle: the rest of this frame's input is skipped.
  bool applyEvent(const ws::ContactEvent& event, ws::Cell newAnchor);
  void evaluateLine(ws::Line line);
  bool revealCursor();
  void setCursor(ws::Cell to);
  void keyTap();
  void clearAnchor();
  void showHint();
  void openMenu();
  void exitToApps();
  void handleKeys();
  void tickElapsed(unsigned long now);
  int contentTop() const;
  void formatStatus(char* out, size_t cap, Status status) const;

  // Two cells in one word (the drag preview's; the anchor and the key cursor), so render() never
  // sees half an update and loop() never waits on a render (~0.55 s FAST) to change them: a key
  // press or tap made meanwhile would be lost.
  static uint32_t packCells(ws::Cell a, ws::Cell b);
  static void unpackCells(uint32_t packed, ws::Cell& a, ws::Cell& b);
  // Publishes anchor and puzzle->cursor to render().
  void publishMarks();

  std::unique_ptr<ws::Puzzle> puzzle;
  std::unique_ptr<char[]> saveText;  // ws::PUZZLE_TEXT_MAX, the puzzle file's text
  ws::Prefs prefs;
  ws::BoardLayout layout;
  ws::Contact contact;
  ws::Cell anchor;  // loop() only; render() reads `marks`
  std::atomic<Status> status{Status::Progress};
  std::atomic<bool> showCursor{false};
  // A touch hid the cursor: the render at the contact's end must happen even on a finished board.
  bool cursorHidPending = false;
  bool failed = false;  // out of memory: a message and Back
  // onEnter is done. A render already owed when the replace released the lock runs before onEnter;
  // it draws nothing rather than a half-loaded puzzle or the out-of-memory screen.
  std::atomic<bool> ready{false};
  // A grid contact ended without its release edge (a second finger): Back stays unread up to and
  // including that release, so the drag's end cannot count as the edge swipe.
  bool gridReleasePending = false;
  uint32_t rng = 0;
  uint32_t elapsedMs = 0;
  unsigned long lastTickMs = 0;
  std::atomic<uint32_t> dragView{0};
  std::atomic<uint32_t> marks{0};  // packCells(anchor, puzzle->cursor)
  std::atomic<bool> halfPending{false};
  int rendersUntilHalf = 0;  // render task only
  char statusText[64] = {};  // render task only, off its stack
  char bannerTitle[64] = {};
  char bannerDetail[24] = {};
  ButtonNavigator buttonNavigator;
};
