#include "CrosswordClueListActivity.h"

#include <I18n.h>

#include <cstdio>

#include "components/UITheme.h"

namespace fui = freeink::ui;

CrosswordClueListActivity::CrosswordClueListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const cw::Puzzle& puzzle, const cw::Progress& progress)
    : UiListActivity("CrosswordClues", renderer, mappedInput), puzzle(puzzle), progress(progress) {}

void CrosswordClueListActivity::onEnter() {
  UiListActivity::onEnter();
  // Open on the current clue (the first build pulls the viewport to it).
  nav.selected = cw::currentEntry(puzzle, progress);
}

int CrosswordClueListActivity::listCount() const { return puzzle.entryCount + (hasFooter() ? 1 : 0); }

const char* CrosswordClueListActivity::headerTitle() const { return tr(STR_CW_CLUE_LIST); }

void CrosswordClueListActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<CrosswordClueListActivity*>(ctx);
  const cw::Puzzle& p = self->puzzle;
  const int i = index;
  if (i >= p.entryCount) {
    // The footer: who made it (content, as the file gives it).
    item.label = p.author[0] ? p.author : p.copyright;
    item.subtitle = p.author[0] && p.copyright[0] ? p.copyright : nullptr;
    item.enabled = false;
    item.actionValue = static_cast<int16_t>(i);
    return;
  }
  char number[8];
  cw::formatClueLabel(p, i, number, sizeof(number));
  std::snprintf(self->label, sizeof(self->label), "%s %s", number, p.clue(i));
  cw::entryPattern(p, self->progress, i, self->pattern, sizeof(self->pattern));
  item.label = self->label;
  item.subtitle = self->pattern;
  if (cw::entryFull(p, self->progress, i)) item.value = tr(STR_CW_DONE);
  if (i == 0 && p.acrossCount > 0) item.sectionHeading = tr(STR_CW_ACROSS);
  if (i == p.acrossCount && p.acrossCount < p.entryCount) item.sectionHeading = tr(STR_CW_DOWN);
  item.actionValue = static_cast<int16_t>(i);
}

void CrosswordClueListActivity::activateIndex(const int index) {
  if (index < 0 || index >= puzzle.entryCount) return;  // the footer is not a clue
  // The row leaves this screen; a lingering flash would gray an unrelated element.
  app.clearTapFlash();
  nav.selected = index;
  MenuResult result;
  result.action = index;
  setResult(std::move(result));
  finish();
}

void CrosswordClueListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.rowProvider = &CrosswordClueListActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);
  screen.list(props);
}
