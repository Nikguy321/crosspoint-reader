#pragma once

#include <string>

#include "activities/UiListActivity.h"

/**
 * KOReader Sync > Peer & Auto Sync (booksync fork): the peer hotspot's name,
 * password and server, a hub's network and server, the server every other
 * network uses (shown only), how long a sync waits for Wi-Fi, and the automatic
 * syncs on book close and open. Stored in BookSyncStore; saving the peer name
 * adds it to the Wi-Fi list, saving its password writes that entry.
 */
class BookSyncSettingsActivity final : public UiListActivity {
 public:
  explicit BookSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 9;

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
