#pragma once

#include <Sudoku.h>

#include "activities/UiListActivity.h"

// Sudoku's menu, pushed over the game: Hint; Check square / puzzle; Reveal square / puzzle; Fill all
// notes; New puzzle (a second tap confirms while a puzzle is in progress); Difficulty (cycles the
// tier New puzzle makes); Remove notes when placing; Back to puzzle. The first row's subtitle is
// the time and the counts. The tier and the setting change the game's prefs, saved here when
// changed. The game reads a MenuResult (the row's own index) when it closes.
class SudokuMenuActivity final : public UiListActivity {
 public:
  enum Row : uint8_t {
    ROW_HINT,
    ROW_CHECK_SQUARE,
    ROW_CHECK_PUZZLE,
    ROW_REVEAL_SQUARE,
    ROW_REVEAL_PUZZLE,
    ROW_FILL_NOTES,
    ROW_NEW_PUZZLE,
    ROW_DIFFICULTY,
    ROW_REMOVE_NOTES,
    ROW_BACK,
    ROW_COUNT
  };

  // prefs: the game's, which stays on the stack under this menu. solved: the play rows are greyed
  // out. inProgress: New puzzle asks again.
  SudokuMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, sd::Prefs& prefs, const sd::Game& game,
                     bool inProgress, uint32_t elapsedSeconds);

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return ROW_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void refreshValues();
  void finishWith(int action);

  sd::Prefs& prefs;
  const bool solved;
  const bool inProgress;
  const uint32_t elapsedSeconds;
  const uint16_t hints;
  const uint16_t checks;
  const uint16_t reveals;
  bool prefsChanged = false;
  bool newArmed = false;  // New puzzle was tapped once
  char summary[72] = {};
  char nextText[48] = {};
  char difficultyText[48] = {};
  freeink::ui::ListItem rows[ROW_COUNT]{};
};
