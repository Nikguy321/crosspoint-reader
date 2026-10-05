#pragma once

#include "activities/UiListActivity.h"

// The Apps list (a Home row): the games and tools that are not books. Each app is opened with a
// replace, and its Back replaces back to this list, so no stack has to survive sleep or a boot
// (an app resumes on its own: Activity::resumeApp). Back here goes Home with the Apps row
// selected. The rows are decided when the list is built: Crossword and Sudoku need a touch screen.
class AppsActivity final : public UiListActivity {
 public:
  explicit AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  enum class App : uint8_t { WordSearch, Crossword, Sudoku };
  static constexpr int MAX_APPS = 3;

  // The app a Back is leaving: the next Apps list opens with its row selected (once).
  static void selectOnNextOpen(App app);

  void onEnter() override;

 private:
  int listCount() const override { return appCount_; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  // Rows built once in the constructor; apps_[i] is row i's app.
  freeink::ui::ListItem rowItems_[MAX_APPS]{};
  App apps_[MAX_APPS]{};
  int appCount_ = 0;
  static bool hasPendingSelect_;
  static App pendingSelect_;
};
