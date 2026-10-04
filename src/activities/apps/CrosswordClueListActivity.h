#pragma once

#include <Crossword.h>

#include "activities/UiListActivity.h"

// Crossword's clue list, pushed over the game: the Across clues under an ACROSS heading, then
// the Down clues under DOWN, one row each ("14A Clue", up to two lines, the fill pattern such
// as CA__S below it, Done once the word is full), then the author / copyright when the puzzle
// has them. Opens on the current clue; a tap returns its entry (MenuResult action) to the game,
// which jumps there. The puzzle and progress are the game's, read while the game's loop is
// paused under this screen.
class CrosswordClueListActivity final : public UiListActivity {
 public:
  CrosswordClueListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const cw::Puzzle& puzzle,
                            const cw::Progress& progress);

  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  bool hasFooter() const { return puzzle.author[0] != '\0' || puzzle.copyright[0] != '\0'; }

  const cw::Puzzle& puzzle;
  const cw::Progress& progress;
  // One row's text at a time (the provider's pointers live until its next call).
  char label[8 + cw::MAX_CLUE_BYTES + 1] = {};
  char pattern[cw::MAX_SIDE + 1] = {};
};
