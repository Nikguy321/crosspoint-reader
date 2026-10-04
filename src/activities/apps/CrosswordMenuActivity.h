#pragma once

#include <Crossword.h>

#include "activities/UiListActivity.h"

// Crossword's menu, pushed over the game: Clue list; Check letter / word / puzzle; Reveal letter
// / word / puzzle; Clear word; Clear puzzle (a second tap confirms); Puzzles...; Next unsolved;
// Skip filled squares (toggles the game's prefs, saved here when changed); Back to puzzle. The
// first row's subtitle is the time and the counts. The game reads a MenuResult (ACTION_*, the
// row's own index) when it closes.
class CrosswordMenuActivity final : public UiListActivity {
 public:
  enum Row : uint8_t {
    ROW_CLUE_LIST,
    ROW_CHECK_LETTER,
    ROW_CHECK_WORD,
    ROW_CHECK_PUZZLE,
    ROW_REVEAL_LETTER,
    ROW_REVEAL_WORD,
    ROW_REVEAL_PUZZLE,
    ROW_CLEAR_WORD,
    ROW_CLEAR_PUZZLE,
    ROW_PUZZLES,
    ROW_NEXT_UNSOLVED,
    ROW_SKIP_FILLED,
    ROW_BACK,
    ROW_COUNT
  };

  // prefs: the game's, which stays on the stack under this menu. solved: the edits (check,
  // reveal, clear word) are greyed out.
  CrosswordMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, cw::Prefs& prefs, bool solved,
                        uint32_t elapsedSeconds, uint16_t checks, uint16_t reveals);

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return ROW_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void refreshValues();
  void finishWith(int action);

  cw::Prefs& prefs;
  const bool solved;
  const uint32_t elapsedSeconds;
  const uint16_t checks;
  const uint16_t reveals;
  bool prefsChanged = false;
  bool clearArmed = false;  // Clear puzzle was tapped once
  char summary[64] = {};
  freeink::ui::ListItem rows[ROW_COUNT]{};
};
