#include "AppsActivity.h"

#include <I18n.h>

#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

AppsActivity::AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Apps", renderer, mappedInput) {
  fui::ListItem wordSearch;
  wordSearch.label = tr(STR_WORD_SEARCH);
  wordSearch.subtitle = tr(STR_WORD_SEARCH_DESC);
  wordSearch.icon = fui::bitmapFromIcon(icon_word_search_32);  // subtitle rows carry the larger icon
  wordSearch.actionValue = 0;
  rowItems_[0] = wordSearch;
}

const char* AppsActivity::headerTitle() const { return tr(STR_APPS); }

void AppsActivity::activateIndex(const int index) {
  // The row leaves this screen; a lingering flash would gray an unrelated element on the next
  // render.
  app.clearTapFlash();
  nav.selected = index;
  if (index == 0) activityManager.goToWordSearch();
}

// Not finish(): with nothing under this screen it would go Home with no row selected.
void AppsActivity::onBackButton() { onGoHome(HomeMenuItem::APPS); }

void AppsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(APP_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
