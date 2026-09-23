#pragma once

#include <BoardConfig.h>

#if FREEINK_CAP_TOUCH
#include "activities/UiListActivity.h"

class TapZoneSettingsActivity final : public UiListActivity {
 public:
  TapZoneSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;

 private:
  int listCount() const override { return 2; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rows[2]{};
};
#endif
