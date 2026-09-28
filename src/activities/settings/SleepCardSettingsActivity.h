#pragma once

#include <string>

#include "activities/UiListActivity.h"

/**
 * Display > Sleep Screen Cards (X4 Pro): the place the Day, Calendar and Sky
 * cards compute the sun and moon for, the hunting season and its legal-light
 * rule, the Owner card's lines, where the Quote card reads from, and which
 * cards Shuffle may pick. Everything lives in CrossPointSettings and is saved
 * on each change; typed entries are validated and a bad one is not saved.
 */
class SleepCardSettingsActivity final : public UiListActivity {
 public:
  explicit SleepCardSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 16;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void editText(int row, const char* title, const char* initial, size_t maxLength);
  void applyText(int row, const std::string& text);

  // Fixed-capacity row storage, as in BookSyncSettingsActivity: labels are set
  // once, buildScreen() only refreshes the value strings.
  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
  // The last typed entry of the row was refused (shown until the next good one).
  bool invalid_[MENU_ITEMS]{};
};
