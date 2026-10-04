#include "CrosswordMenuActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "CrosswordStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

CrosswordMenuActivity::CrosswordMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, cw::Prefs& prefs,
                                             const bool solved, const uint32_t elapsedSeconds, const uint16_t checks,
                                             const uint16_t reveals)
    : UiListActivity("CrosswordMenu", renderer, mappedInput),
      prefs(prefs),
      solved(solved),
      elapsedSeconds(elapsedSeconds),
      checks(checks),
      reveals(reveals) {}

void CrosswordMenuActivity::onEnter() {
  UiListActivity::onEnter();
  rows[ROW_CLUE_LIST].label = tr(STR_CW_CLUE_LIST);
  rows[ROW_CHECK_LETTER].label = tr(STR_CW_CHECK_LETTER);
  rows[ROW_CHECK_WORD].label = tr(STR_CW_CHECK_WORD);
  rows[ROW_CHECK_PUZZLE].label = tr(STR_CW_CHECK_PUZZLE);
  rows[ROW_REVEAL_LETTER].label = tr(STR_CW_REVEAL_LETTER);
  rows[ROW_REVEAL_WORD].label = tr(STR_CW_REVEAL_WORD);
  rows[ROW_REVEAL_PUZZLE].label = tr(STR_CW_REVEAL_PUZZLE);
  rows[ROW_CLEAR_WORD].label = tr(STR_CW_CLEAR_WORD);
  rows[ROW_CLEAR_PUZZLE].label = tr(STR_CW_CLEAR_PUZZLE);
  rows[ROW_PUZZLES].label = tr(STR_CW_PUZZLES);
  rows[ROW_NEXT_UNSOLVED].label = tr(STR_CW_NEXT_UNSOLVED);
  rows[ROW_SKIP_FILLED].label = tr(STR_CW_SKIP_FILLED);
  rows[ROW_BACK].label = tr(STR_CW_BACK_TO_PUZZLE);
  for (int i = ROW_CHECK_LETTER; i <= ROW_CLEAR_WORD; i++) rows[i].enabled = !solved;
  for (int i = 0; i < ROW_COUNT; i++) rows[i].actionValue = static_cast<int16_t>(i);

  // "4:12 - 2 checks - 1 reveal" (the counts only when not 0).
  char time[16];
  cw::formatElapsed(elapsedSeconds, time, sizeof(time));
  int n = std::snprintf(summary, sizeof(summary), "%s", time);
  if (checks > 0 && n > 0 && static_cast<size_t>(n) < sizeof(summary)) {
    char part[24];
    if (checks == 1) {
      std::snprintf(part, sizeof(part), "%s", tr(STR_CW_ONE_CHECK));
    } else {
      std::snprintf(part, sizeof(part), tr(STR_CW_CHECKS), checks);
    }
    n += std::snprintf(summary + n, sizeof(summary) - n, " - %s", part);
  }
  if (reveals > 0 && n > 0 && static_cast<size_t>(n) < sizeof(summary)) {
    char part[24];
    if (reveals == 1) {
      std::snprintf(part, sizeof(part), "%s", tr(STR_CW_ONE_REVEAL));
    } else {
      std::snprintf(part, sizeof(part), tr(STR_CW_REVEALS), reveals);
    }
    std::snprintf(summary + n, sizeof(summary) - n, " - %s", part);
  }
  rows[ROW_CLUE_LIST].subtitle = summary;
  refreshValues();
}

void CrosswordMenuActivity::onExit() {
  if (prefsChanged && !cw::store::savePrefs(prefs)) LOG_ERR("CW", "Prefs not saved");
  UiListActivity::onExit();
}

const char* CrosswordMenuActivity::headerTitle() const { return tr(STR_CROSSWORD); }

void CrosswordMenuActivity::refreshValues() {
  RenderLock lock(*this);
  rows[ROW_SKIP_FILLED].subtitle = prefs.skipFilled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rows[ROW_CLEAR_PUZZLE].label = clearArmed ? tr(STR_CW_CLEAR_CONFIRM) : tr(STR_CW_CLEAR_PUZZLE);
}

void CrosswordMenuActivity::finishWith(const int action) {
  // The row leaves this screen; a lingering flash would gray an unrelated element.
  app.clearTapFlash();
  MenuResult result;
  result.action = action;
  setResult(std::move(result));
  finish();
}

void CrosswordMenuActivity::activateIndex(const int index) {
  nav.selected = index;
  if (index < 0 || index >= ROW_COUNT) return;
  if (!rows[index].enabled) return;
  switch (index) {
    case ROW_CLEAR_PUZZLE:
      if (!clearArmed) {
        clearArmed = true;
        refreshValues();
        requestUpdate();
        return;
      }
      finishWith(index);
      return;
    case ROW_SKIP_FILLED:
      prefs.skipFilled = !prefs.skipFilled;
      prefsChanged = true;
      refreshValues();
      requestUpdate();
      return;
    case ROW_BACK:
      app.clearTapFlash();
      finish();
      return;
    default:
      finishWith(index);
      return;
  }
}

void CrosswordMenuActivity::buildScreen(UiScreen& screen) {
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
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
