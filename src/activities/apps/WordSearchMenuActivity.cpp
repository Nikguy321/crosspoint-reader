#include "WordSearchMenuActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "WordSearchStore.h"
#include "WordSearchThemeActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

WordSearchMenuActivity::WordSearchMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ws::Prefs& prefs,
                                               const bool hintAvailable)
    : UiListActivity("WordSearchMenu", renderer, mappedInput), prefs(prefs), hintAvailable(hintAvailable) {}

void WordSearchMenuActivity::onEnter() {
  UiListActivity::onEnter();
  rows[ROW_NEW].label = tr(STR_WS_NEW_PUZZLE);
  rows[ROW_DIFFICULTY].label = tr(STR_WS_DIFFICULTY);
  rows[ROW_THEME].label = tr(STR_WS_THEME);
  rows[ROW_HINT].label = tr(STR_WS_SHOW_HINT);
  rows[ROW_HINT].subtitle = tr(STR_WS_HINT_DESC);
  rows[ROW_HINT].enabled = hintAvailable;
  rows[ROW_BACK].label = tr(STR_WS_BACK_TO_PUZZLE);
  for (int i = 0; i < ROW_COUNT; i++) rows[i].actionValue = static_cast<int16_t>(i);
  refreshValues();
}

void WordSearchMenuActivity::onExit() {
  if (prefsChanged && !ws::store::savePrefs(prefs)) LOG_ERR("WS", "Prefs not saved");
  UiListActivity::onExit();
}

const char* WordSearchMenuActivity::headerTitle() const { return tr(STR_WORD_SEARCH); }

// The Difficulty and Theme subtitles (both apply from the next puzzle).
void WordSearchMenuActivity::refreshValues() {
  // The theme's title first, outside the lock: a card theme's comes from the card.
  const char* title = tr(STR_WS_RANDOM);
  if (!prefs.randomChoice()) {
    const int builtin = ws::findBuiltinTheme(prefs.choice);
    if (builtin >= 0) {
      title = ws::builtinTheme(static_cast<size_t>(builtin)).title;
    } else if (ws::store::readFileTitle(prefs.choice, themeTitle, sizeof(themeTitle))) {
      title = themeTitle;
    } else {
      const char* name = ws::fileNameFromKey(prefs.choice);
      title = name ? name : prefs.choice;
    }
  }
  const char* difficulty = tr(STR_WS_MEDIUM);
  if (prefs.difficulty == ws::Difficulty::Easy) difficulty = tr(STR_WS_EASY);
  if (prefs.difficulty == ws::Difficulty::Hard) difficulty = tr(STR_WS_HARD);

  RenderLock lock(*this);
  std::snprintf(difficultyText, sizeof(difficultyText), tr(STR_WS_NEXT_PUZZLE), difficulty);
  std::snprintf(themeText, sizeof(themeText), tr(STR_WS_NEXT_PUZZLE), title);
  rows[ROW_DIFFICULTY].subtitle = difficultyText;
  rows[ROW_THEME].subtitle = themeText;
}

void WordSearchMenuActivity::finishWith(const int action) {
  // The row leaves this screen; a lingering flash would gray an unrelated element.
  app.clearTapFlash();
  MenuResult result;
  result.action = action;
  setResult(std::move(result));
  finish();
}

void WordSearchMenuActivity::activateIndex(const int index) {
  nav.selected = index;
  switch (index) {
    case ROW_NEW:
      finishWith(ACTION_NEW_PUZZLE);
      return;
    case ROW_DIFFICULTY:
      prefs.difficulty = static_cast<ws::Difficulty>((static_cast<int>(prefs.difficulty) + 1) % ws::DIFFICULTY_COUNT);
      prefsChanged = true;
      refreshValues();
      requestUpdate();
      return;
    case ROW_THEME: {
      auto picker = makeUniqueNoThrow<WordSearchThemeActivity>(renderer, mappedInput, prefs);
      if (!picker) {
        LOG_ERR("WS", "OOM: theme picker");
        return;
      }
      app.clearTapFlash();
      startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
        if (!std::holds_alternative<MenuResult>(result.data)) return;  // Back: no change
        prefsChanged = true;
        refreshValues();
      });
      return;
    }
    case ROW_HINT:
      if (hintAvailable) finishWith(ACTION_SHOW_HINT);
      return;
    case ROW_BACK:
      app.clearTapFlash();
      finish();
      return;
    default:
      return;
  }
}

void WordSearchMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows;
  props.count = static_cast<uint16_t>(ROW_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
