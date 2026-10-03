#pragma once

#include "activities/UiListActivity.h"

// The Apps list (a Home row): the games and tools that are not books. Each app is opened with a
// replace, and its Back replaces back to this list, so no stack has to survive sleep or a boot
// (an app resumes on its own: Activity::resumeApp). Back here goes Home with the Apps row
// selected.
class AppsActivity final : public UiListActivity {
 public:
  explicit AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int APP_COUNT = 1;

 private:
  int listCount() const override { return APP_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  // Static rows, built once in the constructor.
  freeink::ui::ListItem rowItems_[APP_COUNT]{};
};
