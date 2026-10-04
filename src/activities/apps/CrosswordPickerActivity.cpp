#include "CrosswordPickerActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

// Collection rows: Built-in, On the card, then the packs.
constexpr int ROW_BUILTIN = 0;
constexpr int ROW_CARD = 1;
constexpr int FIRST_PACK = 2;

}  // namespace

CrosswordPickerActivity::CrosswordPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 cw::Prefs& prefs, std::unique_ptr<cw::Puzzle>& staged,
                                                 const char* currentKey)
    : UiListActivity("CrosswordPicker", renderer, mappedInput), prefs(prefs), staged(staged) {
  std::snprintf(this->currentKey, sizeof(this->currentKey), "%s", currentKey ? currentKey : "");
}

void CrosswordPickerActivity::onEnter() {
  UiListActivity::onEnter();
  if (!solved.load()) LOG_ERR("CW", "Solved list unreadable: no marks");
  packs = makeUniqueNoThrow<cw::store::FileEntry[]>(cw::MAX_PACKS);
  files = makeUniqueNoThrow<cw::store::FileEntry[]>(cw::MAX_FOLDER_FILES);
  status = makeUniqueNoThrow<Status[]>(cw::MAX_FOLDER_FILES);
  if (!packs || !files || !status) LOG_ERR("CW", "OOM: picker lists (card collections hidden)");
  // Open on the last collection's puzzles (Back goes up to the collections).
  showCollections();
  if (prefs.collection[0] == '\0' || !showPuzzles(prefs.collection)) showPuzzles(cw::COLLECTION_BUILTIN);
}

void CrosswordPickerActivity::showCollections() {
  level = Level::Collections;
  packCount = packs ? cw::store::listPacks(packs.get(), cw::MAX_PACKS, &rootFiles) : 0;
  builtinSolved = 0;
  char key[cw::MAX_SOURCE_KEY + 1];
  for (size_t i = 0; i < cw::builtinCount(); i++) {
    std::snprintf(key, sizeof(key), "%s%s", cw::BUILTIN_KEY_PREFIX, cw::builtinPuzzle(i).id);
    if (solved.has(key, cw::builtinPuzzle(i).fnv)) builtinSolved++;
  }
  nav.selected = 0;
  for (int i = 0; i < listCount(); i++) {
    char c[cw::MAX_COLLECTION_KEY + 1];
    if (collectionAt(i, c, sizeof(c)) && std::strcmp(c, collection) == 0) nav.selected = i;
  }
}

bool CrosswordPickerActivity::showPuzzles(const char* key) {
  const bool builtin = std::strcmp(key, cw::COLLECTION_BUILTIN) == 0;
  if (!builtin && (!files || !status)) return false;
  int count = 0;
  bool more = false;
  if (builtin) {
    count = static_cast<int>(cw::builtinCount());
  } else {
    count = cw::store::listFiles(key, files.get(), cw::MAX_FOLDER_FILES, &more);
    if (count == 0 && std::strcmp(key, cw::COLLECTION_CARD) != 0) return false;  // a pack that is gone
  }
  RenderLock lock(*this);
  std::snprintf(collection, sizeof(collection), "%s", key);
  level = Level::Puzzles;
  puzzleCount = count;
  capped = more;
  nav.selected = 0;
  for (int i = 0; i < count; i++) {
    char k[cw::MAX_SOURCE_KEY + 1];
    if (!keyAt(i, k, sizeof(k))) continue;
    if (status) {
      status[i] = Status{};
      status[i].solved = solved.has(k, builtin ? cw::builtinPuzzle(static_cast<size_t>(i)).fnv : 0);
      if (builtin) {
        status[i].w = cw::builtinPuzzle(static_cast<size_t>(i)).w;
        status[i].h = cw::builtinPuzzle(static_cast<size_t>(i)).h;
      }
    }
    if (std::strcmp(k, currentKey) == 0) nav.selected = i;
  }
  return true;
}

int CrosswordPickerActivity::listCount() const {
  if (level == Level::Collections) return FIRST_PACK + packCount;
  return puzzleCount + (capped ? 1 : 0);
}

const char* CrosswordPickerActivity::headerTitle() const {
  if (level == Level::Collections) return tr(STR_CW_PUZZLES);
  if (std::strcmp(collection, cw::COLLECTION_BUILTIN) == 0) return tr(STR_CW_BUILT_IN);
  if (std::strcmp(collection, cw::COLLECTION_CARD) == 0) return tr(STR_CW_ON_CARD);
  return collection + sizeof(cw::COLLECTION_PACK_PREFIX) - 1;  // the pack's folder name
}

bool CrosswordPickerActivity::collectionAt(const int index, char* out, const size_t cap) const {
  int n = -1;
  if (index == ROW_BUILTIN) {
    n = std::snprintf(out, cap, "%s", cw::COLLECTION_BUILTIN);
  } else if (index == ROW_CARD) {
    n = std::snprintf(out, cap, "%s", cw::COLLECTION_CARD);
  } else if (index >= FIRST_PACK && index < FIRST_PACK + packCount && packs) {
    n = std::snprintf(out, cap, "%s%s", cw::COLLECTION_PACK_PREFIX, packs[index - FIRST_PACK].name);
  }
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool CrosswordPickerActivity::keyAt(const int index, char* out, const size_t cap) const {
  if (index < 0 || index >= puzzleCount) return false;
  if (std::strcmp(collection, cw::COLLECTION_BUILTIN) == 0) {
    const int n = std::snprintf(out, cap, "%s%s", cw::BUILTIN_KEY_PREFIX, cw::builtinPuzzle(index).id);
    return n > 0 && static_cast<size_t>(n) < cap;
  }
  return files && cw::store::fileKey(collection, files[index].name, out, cap);
}

const char* CrosswordPickerActivity::reasonFor(const Status& s, char* buf, const size_t cap) {
  switch (s.error) {
    case cw::Error::None:
      return nullptr;
    case cw::Error::TooBig:
      std::snprintf(buf, cap, tr(STR_CW_ERR_TOO_BIG), s.w, s.h);
      return buf;
    case cw::Error::TooSmall:
      return tr(STR_CW_ERR_TOO_SMALL);
    case cw::Error::FileTooLarge:
      return tr(STR_CW_ERR_FILE_TOO_LARGE);
    case cw::Error::Rebus:
      return tr(STR_CW_ERR_REBUS);
    case cw::Error::Locked:
      return tr(STR_CW_ERR_LOCKED);
    case cw::Error::Diagramless:
      return tr(STR_CW_ERR_DIAGRAMLESS);
    case cw::Error::Barred:
      return tr(STR_CW_ERR_BARRED);
    case cw::Error::NoSolution:
      return tr(STR_CW_ERR_NO_SOLUTION);
    case cw::Error::NotCrossword:
      return tr(STR_CW_ERR_NOT_CROSSWORD);
    case cw::Error::Damaged:
      return tr(STR_CW_ERR_DAMAGED);
    case cw::Error::TooManyClues:
      return tr(STR_CW_ERR_TOO_MANY_CLUES);
    case cw::Error::BadNumbering:
      return tr(STR_CW_ERR_NUMBERING);
    case cw::Error::ClueMismatch:
      return tr(STR_CW_ERR_CLUES);
    case cw::Error::OutOfMemory:
      return tr(STR_MEMORY_ERROR);
  }
  return nullptr;
}

// Rows on demand (fui::ListProps::rowProvider): labels point at flash, the lists, or the
// one-row scratch buffers.
void CrosswordPickerActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<CrosswordPickerActivity*>(ctx);
  const int i = index;
  item.actionValue = static_cast<int16_t>(i);
  if (self->level == Level::Collections) {
    if (i == ROW_BUILTIN) {
      item.label = tr(STR_CW_BUILT_IN);
      std::snprintf(self->subtitle, sizeof(self->subtitle), tr(STR_CW_SOLVED_COUNT), self->builtinSolved,
                    static_cast<int>(cw::builtinCount()));
      item.subtitle = self->subtitle;
    } else if (i == ROW_CARD) {
      item.label = tr(STR_CW_ON_CARD);
      if (self->rootFiles == 0 && self->packCount == 0) {
        item.subtitle = tr(STR_CW_EMPTY_CARD);
        item.enabled = false;
      } else {
        std::snprintf(self->subtitle, sizeof(self->subtitle), tr(STR_CW_PUZZLE_COUNT), self->rootFiles);
        item.subtitle = self->subtitle;
      }
    } else if (self->packs && i - FIRST_PACK < self->packCount) {
      item.label = self->packs[i - FIRST_PACK].name;
    }
    char c[cw::MAX_COLLECTION_KEY + 1];
    if (self->collectionAt(i, c, sizeof(c)) && std::strcmp(c, self->prefs.collection) == 0) {
      item.value = tr(STR_SELECTED);
    }
    return;
  }
  if (i >= self->puzzleCount) {
    std::snprintf(self->subtitle, sizeof(self->subtitle), tr(STR_CW_LIST_CAPPED), cw::MAX_FOLDER_FILES);
    item.label = self->subtitle;
    item.enabled = false;
    return;
  }
  const bool builtin = std::strcmp(self->collection, cw::COLLECTION_BUILTIN) == 0;
  const Status s = self->status ? self->status[i] : Status{};
  if (builtin) {
    item.label = cw::builtinPuzzle(static_cast<size_t>(i)).title;
  } else {
    cw::displayName(self->files[i].name, self->label, sizeof(self->label));
    // Two files that differ only in their extension (a puzzle as .ipuz and .puz) keep it.
    char other[sizeof(self->label)];
    for (int j = 0; j < self->puzzleCount; j++) {
      if (j == i) continue;
      cw::displayName(self->files[j].name, other, sizeof(other));
      if (std::strcmp(other, self->label) == 0) {
        std::snprintf(self->label, sizeof(self->label), "%s", self->files[i].name);
        break;
      }
    }
    item.label = self->label;
  }
  const char* reason = reasonFor(s, self->subtitle, sizeof(self->subtitle));
  if (reason) {
    item.subtitle = reason;
  } else if (s.w > 0 && s.h > 0) {
    std::snprintf(self->subtitle, sizeof(self->subtitle), tr(STR_CW_SIZE), s.w, s.h);
    item.subtitle = self->subtitle;
  }
  if (s.solved) item.value = tr(STR_CW_SOLVED);
}

void CrosswordPickerActivity::choose(const int index) {
  char key[cw::MAX_SOURCE_KEY + 1];
  if (!keyAt(index, key, sizeof(key))) return;
  if (!staged) staged = makeUniqueNoThrow<cw::Puzzle>();
  if (!staged) {
    LOG_ERR("CW", "OOM: staged puzzle");
    return;
  }
  const cw::LoadStatus st = cw::store::loadSource(key, *staged);
  if (!st.ok()) {
    staged.reset();
    if (status) {
      RenderLock lock(*this);
      status[index].error = st.error;
      status[index].w = st.width;
      status[index].h = st.height;
    }
    nav.selected = index;
    requestUpdate();
    return;
  }
  std::snprintf(prefs.collection, sizeof(prefs.collection), "%s", collection);
  // The row leaves this screen; a lingering flash would gray an unrelated element.
  app.clearTapFlash();
  MenuResult result;
  result.action = index;
  setResult(std::move(result));
  finish();
}

void CrosswordPickerActivity::activateIndex(const int index) {
  nav.selected = index;
  if (level == Level::Puzzles) {
    if (index < puzzleCount) choose(index);
    return;
  }
  char c[cw::MAX_COLLECTION_KEY + 1];
  if (!collectionAt(index, c, sizeof(c))) return;
  if (index == ROW_CARD && rootFiles == 0 && packCount == 0) return;  // nothing on the card
  app.clearTapFlash();
  if (showPuzzles(c)) requestUpdate();
}

void CrosswordPickerActivity::onBackButton() {
  if (level == Level::Puzzles) {
    app.clearTapFlash();
    {
      RenderLock lock(*this);
      showCollections();
    }
    requestUpdate();
    return;
  }
  finish();
}

void CrosswordPickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.rowProvider = &CrosswordPickerActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
