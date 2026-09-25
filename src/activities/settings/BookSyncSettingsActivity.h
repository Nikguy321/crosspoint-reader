#pragma once

#include <string>

#include "activities/UiListActivity.h"

/**
 * KOReader Sync > Peer & Auto Sync (booksync fork): the peer hotspot's name and
 * server, how long a sync waits for Wi-Fi, and the automatic syncs on book close
 * and open. Stored in BookSyncStore.
 */
class BookSyncSettingsActivity final : public UiListActivity {
 public:
  explicit BookSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 5;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Fixed-capacity row storage, as in KOReaderSettingsActivity: labels are set
  // once, buildScreen() only refreshes the value strings.
  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
};
