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
  apps_[appCount_] = App::WordSearch;
  rowItems_[appCount_++] = wordSearch;
  // Crossword is typed on its on-screen keyboard: touch boards only.
  if (mappedInput.hasTouch()) {
    fui::ListItem crossword;
    crossword.label = tr(STR_CROSSWORD);
    crossword.subtitle = tr(STR_CROSSWORD_DESC);
    crossword.icon = fui::bitmapFromIcon(icon_crossword_32);
    apps_[appCount_] = App::Crossword;
    rowItems_[appCount_++] = crossword;
  }
  for (int i = 0; i < appCount_; i++) rowItems_[i].actionValue = static_cast<int16_t>(i);
}

bool AppsActivity::hasPendingSelect_ = false;
AppsActivity::App AppsActivity::pendingSelect_ = AppsActivity::App::WordSearch;

void AppsActivity::selectOnNextOpen(const App app) {
  pendingSelect_ = app;
  hasPendingSelect_ = true;
}

void AppsActivity::onEnter() {
  UiListActivity::onEnter();
  if (!hasPendingSelect_) return;
  hasPendingSelect_ = false;
  for (int i = 0; i < appCount_; i++) {
    if (apps_[i] == pendingSelect_) nav.selected = i;
  }
}

const char* AppsActivity::headerTitle() const { return tr(STR_APPS); }

void AppsActivity::activateIndex(const int index) {
  // The row leaves this screen; a lingering flash would gray an unrelated element on the next
  // render.
  app.clearTapFlash();
  nav.selected = index;
  if (index < 0 || index >= appCount_) return;
  switch (apps_[index]) {
    case App::WordSearch:
      activityManager.goToWordSearch();
      return;
    case App::Crossword:
      activityManager.goToCrossword();
      return;
  }
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
  props.count = static_cast<uint16_t>(appCount_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
