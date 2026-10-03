#include "WordSearchActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WordSearchDraw.h"
#include "WordSearchMenuActivity.h"
#include "WordSearchStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

using Button = MappedInputManager::Button;

constexpr unsigned long KEY_LONG_PRESS_MS = 600;
// One loop pass that took longer (a card write, the menu over the game) counts as this much.
constexpr unsigned long ELAPSED_STEP_MAX_MS = 1000;
constexpr uint32_t ELAPSED_MAX_MS = ws::ELAPSED_MAX * 1000u;
// The theme pick's stream, apart from the generator's (both come from one seed).
constexpr uint32_t THEME_PICK_SALT = 0x9E3779B9u;

}  // namespace

WordSearchActivity::WordSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(NAME, renderer, mappedInput) {
  dragView.store(packCells(ws::Cell{}, ws::Cell{}));
  marks.store(packCells(ws::Cell{}, ws::Cell{}));
}

uint32_t WordSearchActivity::packCells(const ws::Cell a, const ws::Cell b) {
  return static_cast<uint32_t>(static_cast<uint8_t>(a.row)) | static_cast<uint32_t>(static_cast<uint8_t>(a.col)) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(b.row)) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(b.col)) << 24;
}

void WordSearchActivity::unpackCells(const uint32_t packed, ws::Cell& a, ws::Cell& b) {
  a = ws::makeCell(static_cast<int8_t>(packed & 0xFF), static_cast<int8_t>((packed >> 8) & 0xFF));
  b = ws::makeCell(static_cast<int8_t>((packed >> 16) & 0xFF), static_cast<int8_t>((packed >> 24) & 0xFF));
}

void WordSearchActivity::publishMarks() { marks.store(packCells(anchor, puzzle->cursor)); }

int WordSearchActivity::contentTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
}

void WordSearchActivity::onEnter() {
  Activity::onEnter();
  puzzle = makeUniqueNoThrow<ws::Puzzle>();
  saveText = makeUniqueNoThrow<char[]>(ws::PUZZLE_TEXT_MAX);
  if (!puzzle || !saveText) {
    LOG_ERR("WS", "OOM: puzzle");
    failed = true;
    ready.store(true);
    requestUpdate();
    return;
  }
  rng = esp_random();
  ws::store::loadPrefs(prefs);
  if (ws::store::loadPuzzle(*puzzle, saveText.get(), ws::PUZZLE_TEXT_MAX)) {
    layout = ws::computeLayout(puzzle->difficulty, contentTop());
    elapsedMs = puzzle->elapsedSeconds * 1000u;
    publishMarks();
  } else if (!startPuzzle(esp_random(), prefs.difficulty, nullptr, false)) {
    LOG_ERR("WS", "No puzzle could be made");
    failed = true;
  }
  halfPending = true;
  ready.store(true);
  requestUpdate();
}

void WordSearchActivity::onExit() {
  // ActivityManager holds the render lock here, so nothing reads the puzzle meanwhile.
  if (puzzle && saveText && !failed) {
    if (!puzzle->complete()) puzzle->elapsedSeconds = elapsedMs / 1000;
    const size_t len = ws::formatPuzzle(*puzzle, saveText.get(), ws::PUZZLE_TEXT_MAX);
    if (!ws::store::savePuzzleText(saveText.get(), len)) LOG_ERR("WS", "Puzzle not saved on exit");
  }
  puzzle.reset();
  saveText.reset();
  Activity::onExit();
}

bool WordSearchActivity::pickTheme(uint32_t& pickRng, const bool random, const bool builtinsOnly, char* key,
                                   const size_t cap) {
  if (!random && !prefs.randomChoice()) {
    std::snprintf(key, cap, "%s", prefs.choice);
    return true;
  }
  const size_t builtins = ws::builtinThemeCount();
  std::unique_ptr<ws::store::ThemeKey[]> files;
  if (!builtinsOnly) files = makeUniqueNoThrow<ws::store::ThemeKey[]>(ws::MAX_THEME_FILES);
  const int fileCount = files ? ws::store::listThemeFiles(files.get(), ws::MAX_THEME_FILES) : 0;
  std::vector<std::string_view> keys;
  keys.reserve(builtins + static_cast<size_t>(fileCount));
  for (size_t i = 0; i < builtins; i++) keys.emplace_back(ws::builtinTheme(i).key);
  for (int i = 0; i < fileCount; i++) keys.emplace_back(files[i].key);
  const int index = ws::pickRandomTheme(pickRng, keys.data(), keys.size(), prefs);
  if (index < 0) return false;
  std::snprintf(key, cap, "%.*s", static_cast<int>(keys[index].size()), keys[index].data());
  return true;
}

bool WordSearchActivity::generate(const char* key, const uint32_t seed, const ws::Difficulty difficulty,
                                  ws::Puzzle& out, bool* themeLoaded) {
  std::unique_ptr<char[]> fileText;  // a card theme's text, which the word views point into
  ws::ThemeWords words;
  const bool loaded = ws::store::loadTheme(key, fileText, words);
  if (themeLoaded) *themeLoaded = loaded;
  if (!loaded) return false;
  if (!ws::generatePuzzle(words.words.data(), words.words.size(), difficulty, seed, out)) {
    LOG_INF("WS", "Theme %s made no puzzle (seed %lu)", key, static_cast<unsigned long>(seed));
    return false;
  }
  std::snprintf(out.themeKey, sizeof(out.themeKey), "%s", key);
  std::snprintf(out.themeTitle, sizeof(out.themeTitle), "%s", words.title);
  return true;
}

bool WordSearchActivity::startPuzzle(const uint32_t seed, const ws::Difficulty difficulty, const char* themeKey,
                                     const bool strict) {
  // Built apart and copied in under the lock: generating takes a while and render() must never
  // see a half-made grid.
  auto fresh = makeUniqueNoThrow<ws::Puzzle>();
  if (!fresh) {
    LOG_ERR("WS", "OOM: new puzzle");
    return false;
  }
  uint32_t pickRng = seed ^ THEME_PICK_SALT;
  char key[ws::MAX_THEME_KEY + 1] = {};
  bool ok = false;
  if (themeKey) {
    std::snprintf(key, sizeof(key), "%s", themeKey);
    ok = generate(key, seed, difficulty, *fresh);
  } else {
    bool loaded = true;
    ok = pickTheme(pickRng, false, false, key, sizeof(key)) && generate(key, seed, difficulty, *fresh, &loaded);
    if (!ok && !prefs.randomChoice()) {
      // A chosen theme that is gone (a deleted or broken file) stops being the choice, silently;
      // one that cannot fill this difficulty stays chosen and Random fills in this once.
      if (!loaded) prefs.choice[0] = '\0';
      ok = pickTheme(pickRng, true, false, key, sizeof(key)) && generate(key, seed, difficulty, *fresh);
    }
    if (!ok && ws::fileNameFromKey(key)) {
      // Random drew a card file that is no theme (too few words) or too small for this difficulty:
      // Random again over the built-in themes, still avoiding the recent ones.
      ok = pickTheme(pickRng, true, true, key, sizeof(key)) && generate(key, seed, difficulty, *fresh);
    }
  }
  if (!ok && !strict) {
    // Any built-in theme makes a puzzle at every difficulty (the host tests check them all).
    const size_t count = ws::builtinThemeCount();
    const size_t first = ws::randomBelow(pickRng, static_cast<uint32_t>(count));
    for (size_t i = 0; i < count && !ok; i++) {
      std::snprintf(key, sizeof(key), "%s", ws::builtinTheme((first + i) % count).key);
      ok = generate(key, seed, difficulty, *fresh);
    }
  }
  if (!ok) return false;
  ws::rememberTheme(prefs, key);
  if (!ws::store::savePrefs(prefs)) LOG_ERR("WS", "Prefs not saved");
  installPuzzle(*fresh);
  savePuzzle();
  return true;
}

void WordSearchActivity::installPuzzle(const ws::Puzzle& fresh) {
  {
    RenderLock lock(*this);
    *puzzle = fresh;
    layout = ws::computeLayout(puzzle->difficulty, contentTop());
    anchor = ws::Cell{};
    status = Status::Progress;
    publishMarks();
  }
  contact = ws::Contact{};
  dragView.store(packCells(ws::Cell{}, ws::Cell{}));
  elapsedMs = 0;
  lastTickMs = 0;
  halfPending = true;
  requestUpdate();
}

void WordSearchActivity::savePuzzle() {
  size_t len = 0;
  {
    // A consistent snapshot; the card write happens outside the lock.
    RenderLock lock(*this);
    if (!puzzle->complete()) puzzle->elapsedSeconds = elapsedMs / 1000;
    len = ws::formatPuzzle(*puzzle, saveText.get(), ws::PUZZLE_TEXT_MAX);
  }
  if (!ws::store::savePuzzleText(saveText.get(), len)) LOG_ERR("WS", "Puzzle not saved");
}

void WordSearchActivity::tickElapsed(const unsigned long now) {
  if (lastTickMs != 0 && !puzzle->complete()) {
    const unsigned long step = now - lastTickMs;
    elapsedMs += step > ELAPSED_STEP_MAX_MS ? ELAPSED_STEP_MAX_MS : step;
    if (elapsedMs > ELAPSED_MAX_MS) elapsedMs = ELAPSED_MAX_MS;
  }
  lastTickMs = now != 0 ? now : 1;
}

void WordSearchActivity::evaluateLine(const ws::Line line) {
  // loop() is the puzzle's only writer, so reading it takes no lock; only a found word does.
  const ws::MatchResult match = ws::matchLine(*puzzle, line);
  if (match.kind != ws::MatchKind::Found) {
    status = match.kind == ws::MatchKind::AlreadyFound ? Status::AlreadyFound : Status::NoMatch;
    return;
  }
  {
    RenderLock lock(*this);
    ws::markFound(*puzzle, match.word, line);
    if (puzzle->complete()) puzzle->elapsedSeconds = elapsedMs / 1000;
  }
  status = Status::Progress;
  savePuzzle();
}

bool WordSearchActivity::applyEvent(const ws::ContactEvent& event, const ws::Cell newAnchor) {
  using Kind = ws::ContactEvent::Kind;
  switch (event.kind) {
    case Kind::None:
      return false;
    case Kind::Began:
      // A touch hides the key cursor; the render when the contact moves or ends carries it.
      if (showCursor.exchange(false)) cursorHidPending = true;
      return false;
    case Kind::EndMoved:
      // A finished board takes no more selections, so it shows none being made either.
      if (puzzle->complete()) return false;
      dragView.store(packCells(contact.start, contact.end));
      requestUpdate();
      return false;
    case Kind::OutsideEnded:
      switch (ws::hitButton(layout, puzzle->complete(), event.x0, event.y0, event.x1, event.y1)) {
        case ws::ButtonHit::Menu:
          openMenu();
          return true;
        case ws::ButtonHit::NewPuzzle:
          startPuzzle(esp_random(), prefs.difficulty, nullptr, false);
          return true;
        case ws::ButtonHit::None:
          return false;
      }
      return false;
    case Kind::Line:
    case Kind::Anchored:
    case Kind::AnchorCleared:
      break;
  }
  dragView.store(packCells(ws::Cell{}, ws::Cell{}));
  const bool hidCursor = cursorHidPending;
  cursorHidPending = false;
  // A finished board takes no more selections.
  if (puzzle->complete()) {
    if (hidCursor) requestUpdate();
    return false;
  }
  anchor = newAnchor;
  status = Status::Progress;
  publishMarks();
  if (event.kind == Kind::Line) evaluateLine(event.line);
  requestUpdate();
  return false;
}

// The first key press only shows the cursor where it is.
bool WordSearchActivity::revealCursor() {
  if (showCursor) return false;
  showCursor = true;
  requestUpdate();
  return true;
}

void WordSearchActivity::setCursor(const ws::Cell to) {
  if (revealCursor()) return;
  puzzle->cursor = to;
  publishMarks();
  requestUpdate();
}

void WordSearchActivity::keyTap() {
  if (revealCursor()) return;
  ws::Cell newAnchor = anchor;
  const ws::ContactEvent event = ws::tapCell(newAnchor, puzzle->cursor);
  applyEvent(event, newAnchor);
}

void WordSearchActivity::clearAnchor() {
  if (!anchor.valid()) return;
  anchor = ws::Cell{};
  status = Status::Progress;
  publishMarks();
  requestUpdate();
}

void WordSearchActivity::showHint() {
  {
    RenderLock lock(*this);
    // One at a time: the ring stays on its word until that word is found.
    if (puzzle->hintWord < 0 || puzzle->words[puzzle->hintWord].found) {
      const int word = ws::pickHintWord(*puzzle, rng);
      if (word < 0) return;
      puzzle->hintWord = static_cast<int8_t>(word);
      if (puzzle->hintsUsed < 255) puzzle->hintsUsed++;
    }
    status = Status::Hint;
  }
  savePuzzle();
  requestUpdate();
}

void WordSearchActivity::openMenu() {
  const bool hintAvailable = !puzzle->complete();
  auto menu = makeUniqueNoThrow<WordSearchMenuActivity>(renderer, mappedInput, prefs, hintAvailable);
  if (!menu) {
    LOG_ERR("WS", "OOM: menu");
    return;
  }
  dragView.store(packCells(ws::Cell{}, ws::Cell{}));
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    lastTickMs = 0;  // the time in the menu does not count
    if (result.isCancelled) return;
    const auto* menuResult = std::get_if<MenuResult>(&result.data);
    if (!menuResult) return;
    if (menuResult->action == WordSearchMenuActivity::ACTION_NEW_PUZZLE) {
      startPuzzle(esp_random(), prefs.difficulty, nullptr, false);
    } else if (menuResult->action == WordSearchMenuActivity::ACTION_SHOW_HINT) {
      showHint();
    }
  });
}

void WordSearchActivity::exitToApps() { activityManager.goToApps(); }

void WordSearchActivity::handleKeys() {
  const bool complete = puzzle->complete();
  if (BoardConfig::isX4Pro()) {
    // Two edge keys: a long press acts (it swallows its own release), a short one steps.
    if (mappedInput.wasLongPressed(Button::Down, KEY_LONG_PRESS_MS)) {
      if (complete) {
        startPuzzle(esp_random(), prefs.difficulty, nullptr, false);
      } else {
        keyTap();
      }
      return;
    }
    if (mappedInput.wasLongPressed(Button::Up, KEY_LONG_PRESS_MS)) {
      clearAnchor();
      return;
    }
    if (mappedInput.wasReleased(Button::Down)) {
      setCursor(ws::stepCursor(puzzle->cursor, 1, puzzle->size));
    } else if (mappedInput.wasReleased(Button::Up)) {
      setCursor(ws::stepCursor(puzzle->cursor, -1, puzzle->size));
    }
    return;
  }
  // Front buttons: no Home key on most of these boards, so holding Confirm opens the menu.
  if (mappedInput.wasLongPressed(Button::Confirm, KEY_LONG_PRESS_MS)) {
    openMenu();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (complete) {
      startPuzzle(esp_random(), prefs.difficulty, nullptr, false);
    } else {
      keyTap();
    }
    return;
  }
  const auto move = [this](const int dRow, const int dCol) {
    setCursor(ws::moveCursor(puzzle->cursor, dRow, dCol, puzzle->size));
  };
  buttonNavigator.onPressAndContinuous({Button::ScreenLeft}, [&move] { move(0, -1); });
  buttonNavigator.onPressAndContinuous({Button::ScreenRight}, [&move] { move(0, 1); });
  buttonNavigator.onPressAndContinuous({Button::ScreenUp}, [&move] { move(-1, 0); });
  buttonNavigator.onPressAndContinuous({Button::ScreenDown}, [&move] { move(1, 0); });
}

void WordSearchActivity::loop() {
  if (failed) {
    if (mappedInput.wasReleased(Button::Back)) exitToApps();
    return;
  }
  tickElapsed(millis());

  // Touch first: the end of a drag must be taken before Back is read, or a quick drag from the
  // first columns would also count as the left-edge Back swipe.
  int x = 0;
  int y = 0;
  const bool held = mappedInput.isScreenTouchHeld(x, y);
  const bool released = mappedInput.wasScreenTouchReleased();
  // The late release of a grid contact that ended early (a second finger) is the grid's too.
  const bool lateGridRelease = released && gridReleasePending;
  if (released) gridReleasePending = false;
  ws::Cell newAnchor = anchor;
  const ws::ContactEvent event = ws::trackContact(contact, newAnchor, layout, held, x, y, released, millis());
  if (event.gridEnded && !released) gridReleasePending = true;
  if (applyEvent(event, newAnchor)) return;
  if (contact.gridActive() || event.gridEnded || lateGridRelease) return;

  // Home long press (its default action, Reader Menu, means nothing outside the reader).
  if (mappedInput.homeButtonAction() == HomeButtonAction::ReaderMenu) {
    openMenu();
    return;
  }
  if (!gridReleasePending && mappedInput.wasReleased(Button::Back)) {
    // A physical Back clears the anchor first; the header arrow and the edge swipe leave.
    if (!mappedInput.wasBackGesture() && anchor.valid()) {
      clearAnchor();
    } else {
      exitToApps();
    }
    return;
  }
  handleKeys();
}

bool WordSearchActivity::handleForcedRefresh() {
  halfPending = true;
  requestUpdate();
  return true;
}

void WordSearchActivity::formatStatus(char* out, const size_t cap, const Status current) const {
  switch (current) {
    case Status::NoMatch:
      std::snprintf(out, cap, "%s", tr(STR_WS_NO_MATCH));
      return;
    case Status::AlreadyFound:
      std::snprintf(out, cap, "%s", tr(STR_WS_ALREADY_FOUND));
      return;
    case Status::Hint:
      if (puzzle->hintWord >= 0 && puzzle->hintWord < puzzle->wordCount) {
        std::snprintf(out, cap, tr(STR_WS_HINT_STATUS), puzzle->words[puzzle->hintWord].letters[0]);
        return;
      }
      break;
    case Status::Progress:
      break;
  }
  std::snprintf(out, cap, tr(STR_WS_FOUND_COUNT), puzzle->foundCount(), puzzle->wordCount);
}

void WordSearchActivity::render(RenderLock&&) {
  if (!ready.load()) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight};
  renderer.clearScreen();
  if (failed || !puzzle) {
    GUI.drawHeader(renderer, header, tr(STR_WORD_SEARCH));
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_MEMORY_ERROR));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  const unsigned long start = millis();

  // What loop() changes without the lock, copied once.
  ws::draw::BoardView view;
  unpackCells(dragView.load(), view.dragStart, view.dragEnd);
  const bool dragging = view.dragStart.valid() && view.dragEnd.valid() && view.dragStart != view.dragEnd;
  unpackCells(marks.load(), view.anchor, view.cursor);
  view.showCursor = showCursor.load();
  formatStatus(statusText, sizeof(statusText), status.load());
  view.status = statusText;
  view.menuLabel = tr(STR_WS_MENU);
  if (puzzle->complete()) {
    char time[16];
    ws::formatElapsed(puzzle->elapsedSeconds, time, sizeof(time));
    std::snprintf(bannerTitle, sizeof(bannerTitle), tr(STR_WS_ALL_FOUND), puzzle->wordCount, time);
    bannerDetail[0] = '\0';
    if (puzzle->hintsUsed == 1) {
      std::snprintf(bannerDetail, sizeof(bannerDetail), "%s", tr(STR_WS_ONE_HINT));
    } else if (puzzle->hintsUsed > 1) {
      std::snprintf(bannerDetail, sizeof(bannerDetail), tr(STR_WS_HINTS), puzzle->hintsUsed);
    }
    view.bannerTitle = bannerTitle;
    view.bannerDetail = bannerDetail;
    view.newPuzzleLabel = tr(STR_WS_NEW_PUZZLE);
  }

  // The theme title is content (built-in English or the file's own), shown as written.
  GUI.drawHeader(renderer, header, puzzle->themeTitle);
  ws::draw::drawBoard(renderer, *puzzle, layout, view);

  // FAST for moves; HALF for a new or loaded puzzle, a forced refresh, and every N frames as the
  // reader does (never in the middle of a drag: it would stall the preview ~1.3 s).
  auto mode = HalDisplay::FAST_REFRESH;
  if (halfPending.exchange(false)) {
    mode = HalDisplay::HALF_REFRESH;
    rendersUntilHalf = SETTINGS.getRefreshFrequency();
  } else if (rendersUntilHalf <= 1 && !dragging) {
    mode = HalDisplay::HALF_REFRESH;
    rendersUntilHalf = SETTINGS.getRefreshFrequency();
  } else if (rendersUntilHalf > 1) {
    rendersUntilHalf--;
  }
  LOG_DBG("WS", "Drawn in %lu ms (%s)", millis() - start, mode == HalDisplay::HALF_REFRESH ? "half" : "fast");
  renderer.displayBuffer(mode);
}

#if CROSSPOINT_BENCH_CONSOLE
bool WordSearchActivity::benchNewPuzzle(const uint32_t seed, const int difficulty, const char* themeKey) {
  if (failed || !puzzle) return false;
  const ws::Difficulty d =
      difficulty >= 0 && difficulty < ws::DIFFICULTY_COUNT ? static_cast<ws::Difficulty>(difficulty) : prefs.difficulty;
  if (!themeKey) {
    // Reproducible: Random would read the card's files and the recent list, which every run changes.
    uint32_t pick = seed ^ THEME_PICK_SALT;
    themeKey = ws::builtinTheme(ws::randomBelow(pick, static_cast<uint32_t>(ws::builtinThemeCount()))).key;
  }
  return startPuzzle(seed, d, themeKey, /*strict=*/true);
}
#endif
