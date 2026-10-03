#include "WordSearchThemeActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "WordSearchStore.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

WordSearchThemeActivity::WordSearchThemeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 ws::Prefs& prefs)
    : UiListActivity("WordSearchTheme", renderer, mappedInput), prefs(prefs) {}

void WordSearchThemeActivity::onEnter() {
  UiListActivity::onEnter();
  builtinCount = static_cast<int>(ws::builtinThemeCount());
  // The card's themes: only each file's title is read here (the words when a puzzle is made).
  files = makeUniqueNoThrow<FileTheme[]>(ws::MAX_THEME_FILES);
  auto keys = makeUniqueNoThrow<ws::store::ThemeKey[]>(ws::MAX_THEME_FILES);
  if (files && keys) {
    const int found = ws::store::listThemeFiles(keys.get(), ws::MAX_THEME_FILES);
    for (int i = 0; i < found; i++) {
      FileTheme& theme = files[fileCount];
      if (!ws::store::readFileTitle(keys[i].key, theme.title, sizeof(theme.title))) continue;
      std::snprintf(theme.key, sizeof(theme.key), "%s", keys[i].key);
      fileCount++;
    }
  } else {
    LOG_ERR("WS", "OOM: theme list");
  }
  // Open on the current choice (the first build pulls the viewport to it).
  nav.selected = 0;
  for (int i = 1; i < listCount(); i++) {
    if (std::strcmp(keyAt(i), prefs.choice) == 0) nav.selected = i;
  }
}

int WordSearchThemeActivity::listCount() const { return 1 + builtinCount + fileCount; }

const char* WordSearchThemeActivity::headerTitle() const { return tr(STR_WS_THEME); }

const char* WordSearchThemeActivity::keyAt(const int index) const {
  if (index <= 0) return "";
  if (index <= builtinCount) return ws::builtinTheme(static_cast<size_t>(index - 1)).key;
  return files[index - 1 - builtinCount].key;
}

// Rows on demand (fui::ListProps::rowProvider): the labels point at flash or at `files`.
void WordSearchThemeActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  const auto* self = static_cast<const WordSearchThemeActivity*>(ctx);
  const int i = index;
  if (i >= self->listCount()) return;
  if (i == 0) {
    item.label = tr(STR_WS_RANDOM);
  } else if (i <= self->builtinCount) {
    item.label = ws::builtinTheme(static_cast<size_t>(i - 1)).title;
    if (i == 1) item.sectionHeading = tr(STR_WS_BUILT_IN);
  } else {
    item.label = self->files[i - 1 - self->builtinCount].title;
    if (i == 1 + self->builtinCount) item.sectionHeading = tr(STR_WS_ON_CARD);
  }
  if (std::strcmp(self->keyAt(i), self->prefs.choice) == 0) item.value = tr(STR_SELECTED);
  if (i > self->builtinCount && self->files[i - 1 - self->builtinCount].unusable) item.value = tr(STR_WS_TOO_FEW_WORDS);
  item.actionValue = static_cast<int16_t>(i);
}

void WordSearchThemeActivity::activateIndex(const int index) {
  if (index > builtinCount && index < listCount()) {
    // Only the title was read for the list: a file that is no theme stays unchosen, marked.
    FileTheme& theme = files[index - 1 - builtinCount];
    std::unique_ptr<char[]> text;
    ws::ThemeWords words;
    if (theme.unusable || !ws::store::loadThemeFile(theme.key, text, words)) {
      LOG_INF("WS", "Not a theme: %s", theme.key);
      theme.unusable = true;
      nav.selected = index;
      requestUpdate();
      return;
    }
  }
  // The row leaves this screen; a lingering flash would gray an unrelated element.
  app.clearTapFlash();
  nav.selected = index;
  std::snprintf(prefs.choice, sizeof(prefs.choice), "%s", keyAt(index));
  MenuResult result;
  result.action = index;
  setResult(std::move(result));
  finish();
}

void WordSearchThemeActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.rowProvider = &WordSearchThemeActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
