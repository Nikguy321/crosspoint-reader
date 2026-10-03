#pragma once

#include <WordSearch.h>

#include <memory>

#include "activities/UiListActivity.h"

// Word Search's theme picker: Random, the built-in themes, then the card's theme files (their
// titles read from the first bytes only). The current choice is marked; picking one sets
// prefs.choice and closes (MenuResult); Back leaves it as it was. A file is read whole when
// picked: one that is no theme (under ws::MIN_FILE_WORDS usable words) stays unchosen and its
// row says so.
class WordSearchThemeActivity final : public UiListActivity {
 public:
  WordSearchThemeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ws::Prefs& prefs);

  void onEnter() override;

 private:
  struct FileTheme {
    char key[ws::MAX_THEME_KEY + 1] = {};
    char title[ws::MAX_TITLE_LEN + 1] = {};
    bool unusable = false;  // picked, and found to be no theme
  };

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  // Row index -> theme key ("" for Random).
  const char* keyAt(int index) const;

  ws::Prefs& prefs;
  std::unique_ptr<FileTheme[]> files;  // ws::MAX_THEME_FILES
  int fileCount = 0;
  int builtinCount = 0;
};
