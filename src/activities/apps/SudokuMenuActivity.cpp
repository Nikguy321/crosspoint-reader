#include "SudokuMenuActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "SudokuDraw.h"
#include "SudokuStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

SudokuMenuActivity::SudokuMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, sd::Prefs& prefs,
                                       const sd::Game& game, const bool inProgress, const uint32_t elapsedSeconds)
    : UiListActivity("SudokuMenu", renderer, mappedInput),
      prefs(prefs),
      solved(game.solved),
      inProgress(inProgress),
      elapsedSeconds(elapsedSeconds),
      hints(game.hints),
      checks(game.checks),
      reveals(game.reveals) {}

void SudokuMenuActivity::onEnter() {
  UiListActivity::onEnter();
  rows[ROW_HINT].label = tr(STR_SD_HINT);
  rows[ROW_CHECK_SQUARE].label = tr(STR_SD_CHECK_SQUARE);
  rows[ROW_CHECK_PUZZLE].label = tr(STR_CW_CHECK_PUZZLE);
  rows[ROW_REVEAL_SQUARE].label = tr(STR_SD_REVEAL_SQUARE);
  rows[ROW_REVEAL_PUZZLE].label = tr(STR_CW_REVEAL_PUZZLE);
  rows[ROW_FILL_NOTES].label = tr(STR_SD_FILL_NOTES);
  rows[ROW_DIFFICULTY].label = tr(STR_WS_DIFFICULTY);
  rows[ROW_REMOVE_NOTES].label = tr(STR_SD_REMOVE_NOTES);
  rows[ROW_BACK].label = tr(STR_CW_BACK_TO_PUZZLE);
  for (int i = ROW_HINT; i <= ROW_FILL_NOTES; i++) rows[i].enabled = !solved;
  for (int i = 0; i < ROW_COUNT; i++) rows[i].actionValue = static_cast<int16_t>(i);

  // "4:12 - 1 hint - 2 checks" (the counts only when not 0).
  char time[16];
  sd::draw::formatElapsed(elapsedSeconds, time, sizeof(time));
  size_t n = static_cast<size_t>(std::snprintf(summary, sizeof(summary), "%s", time));
  const auto add = [&](const uint16_t count, const char* one, const char* many) {
    if (count == 0 || n + 4 >= sizeof(summary)) return;
    char part[24];
    if (count == 1) {
      std::snprintf(part, sizeof(part), "%s", one);
    } else {
      std::snprintf(part, sizeof(part), many, count);
    }
    const int k = std::snprintf(summary + n, sizeof(summary) - n, " - %s", part);
    if (k > 0) n = std::min(sizeof(summary) - 1, n + static_cast<size_t>(k));
  };
  add(hints, tr(STR_WS_ONE_HINT), tr(STR_WS_HINTS));
  add(checks, tr(STR_CW_ONE_CHECK), tr(STR_CW_CHECKS));
  add(reveals, tr(STR_CW_ONE_REVEAL), tr(STR_CW_REVEALS));
  rows[ROW_HINT].subtitle = summary;
  refreshValues();
}

void SudokuMenuActivity::onExit() {
  if (prefsChanged && !sd::store::savePrefs(prefs)) LOG_ERR("SU", "Prefs not saved");
  UiListActivity::onExit();
}

const char* SudokuMenuActivity::headerTitle() const { return tr(STR_SUDOKU); }

void SudokuMenuActivity::refreshValues() {
  // New puzzle's subtitle: the one it makes ("Medium 15"); Difficulty's: "Medium (next puzzle)".
  const unsigned number = prefs.next[prefs.tier < sd::TIER_COUNT ? prefs.tier : 0];
  RenderLock lock(*this);
  std::snprintf(nextText, sizeof(nextText), tr(STR_SD_PUZZLE_NAME), sd::draw::tierName(prefs.tier), number);
  std::snprintf(difficultyText, sizeof(difficultyText), tr(STR_WS_NEXT_PUZZLE), sd::draw::tierName(prefs.tier));
  rows[ROW_NEW_PUZZLE].label = newArmed ? tr(STR_SD_NEW_CONFIRM) : tr(STR_WS_NEW_PUZZLE);
  rows[ROW_NEW_PUZZLE].subtitle = nextText;
  rows[ROW_DIFFICULTY].subtitle = difficultyText;
  rows[ROW_REMOVE_NOTES].subtitle = prefs.removeNotes ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
}

void SudokuMenuActivity::finishWith(const int action) {
  // The row leaves this screen; a lingering flash would gray an unrelated element.
  app.clearTapFlash();
  MenuResult result;
  result.action = action;
  setResult(std::move(result));
  finish();
}

void SudokuMenuActivity::activateIndex(const int index) {
  nav.selected = index;
  if (index < 0 || index >= ROW_COUNT) return;
  if (!rows[index].enabled) return;
  switch (index) {
    case ROW_NEW_PUZZLE:
      if (inProgress && !newArmed) {
        newArmed = true;
        refreshValues();
        requestUpdate();
        return;
      }
      finishWith(index);
      return;
    case ROW_DIFFICULTY:
      prefs.tier = static_cast<uint8_t>((prefs.tier + 1) % sd::TIER_COUNT);
      prefsChanged = true;
      refreshValues();
      requestUpdate();
      return;
    case ROW_REMOVE_NOTES:
      prefs.removeNotes = !prefs.removeNotes;
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

void SudokuMenuActivity::buildScreen(UiScreen& screen) {
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
