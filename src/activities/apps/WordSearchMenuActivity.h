#pragma once

#include <WordSearch.h>

#include "activities/UiListActivity.h"

// Word Search's menu, pushed over the game: New puzzle, Difficulty and Theme (both for the next
// puzzle; they edit the game's prefs, saved here when changed), Show a hint, Back to puzzle.
// The game reads a MenuResult (ACTION_*) when it closes.
class WordSearchMenuActivity final : public UiListActivity {
 public:
  static constexpr int ACTION_NEW_PUZZLE = 0;
  static constexpr int ACTION_SHOW_HINT = 1;

  // prefs: the game's, which stays on the stack under this menu.
  WordSearchMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ws::Prefs& prefs, bool hintAvailable);

  void onEnter() override;
  void onExit() override;

 private:
  enum Row : uint8_t { ROW_NEW, ROW_DIFFICULTY, ROW_THEME, ROW_HINT, ROW_BACK, ROW_COUNT };

  int listCount() const override { return ROW_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void refreshValues();
  void finishWith(int action);

  ws::Prefs& prefs;
  const bool hintAvailable;
  bool prefsChanged = false;
  char difficultyText[48] = {};
  char themeTitle[ws::MAX_TITLE_LEN + 1] = {};
  char themeText[64] = {};
  freeink::ui::ListItem rows[ROW_COUNT]{};
};
