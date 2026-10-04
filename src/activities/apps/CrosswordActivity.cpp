#include "CrosswordActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "AppsActivity.h"
#include "CrossPointSettings.h"
#include "CrosswordClueListActivity.h"
#include "CrosswordDraw.h"
#include "CrosswordMenuActivity.h"
#include "CrosswordPickerActivity.h"
#include "CrosswordStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

using Button = MappedInputManager::Button;

constexpr unsigned long KEY_LONG_PRESS_MS = 600;
// One loop pass that took longer (a card write, a screen over the game) counts as this much.
constexpr unsigned long ELAPSED_STEP_MAX_MS = 1000;
constexpr uint32_t ELAPSED_MAX_MS = cw::ELAPSED_MAX * 1000u;
// A HALF refresh owed by the counter waits for a pause: another word, or this long idle.
constexpr unsigned long IDLE_HALF_MS = 2000;

bool builtinKey(const int index, char* out, const size_t cap) {
  const int n = std::snprintf(out, cap, "%s%s", cw::BUILTIN_KEY_PREFIX, cw::builtinPuzzle(index).id);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool builtinSolved(void* ctx, const int index) {
  char key[cw::MAX_SOURCE_KEY + 1];
  // Key and fnv: a built-in id given new content is not solved by the old one's line.
  return builtinKey(index, key, sizeof(key)) &&
         static_cast<const cw::store::SolvedList*>(ctx)->has(key, cw::builtinPuzzle(index).fnv);
}

struct CardCtx {
  const cw::store::SolvedList* solved;
  const char* collection;
  const cw::store::FileEntry* files;
  uint8_t refused[(cw::MAX_FOLDER_FILES + 7) / 8] = {};  // refused during this search: passed over
};

bool cardSolved(void* ctx, const int index) {
  const auto* c = static_cast<const CardCtx*>(ctx);
  if (c->refused[index / 8] & (1u << (index % 8))) return true;
  char key[cw::MAX_SOURCE_KEY + 1];
  return cw::store::fileKey(c->collection, c->files[index].name, key, sizeof(key)) && c->solved->has(key);
}

}  // namespace

CrosswordActivity::CrosswordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(NAME, renderer, mappedInput) {}

void CrosswordActivity::onEnter() {
  Activity::onEnter();
  // The puzzle itself (~18.6 KB, mostly its clue pool) comes with the first source loaded.
  prog = makeUniqueNoThrow<cw::Progress>();
  shared = makeUniqueNoThrow<Frame>();
  drawn = makeUniqueNoThrow<Frame>();
  if (!prog || !shared || !drawn) {
    LOG_ERR("CW", "OOM: progress");
    failed = true;
  } else {
    cw::store::loadPrefs(prefs);
    // The puzzle being played; if its source is gone (a deleted card file), or on the first run,
    // the first unsolved built-in.
    bool ok = prefs.current[0] != '\0' && openSource(prefs.current).ok();
    if (!ok) ok = openFirstBuiltin();
    if (!ok) {
      LOG_ERR("CW", "No puzzle could be opened");
      failed = true;
    }
  }
  halfPending = true;
  ready.store(true);
  requestUpdate();
}

void CrosswordActivity::onExit() {
  // ActivityManager holds the render lock here, so nothing reads the puzzle meanwhile.
  saveProgress();
  puzzle.reset();
  staged.reset();
  prog.reset();
  shared.reset();
  drawn.reset();
  Activity::onExit();
}

cw::LoadStatus CrosswordActivity::openSource(const char* sourceKey) {
  cw::LoadStatus st;
  // Parsed apart and swapped in under the lock: render() never sees a half-made grid.
  auto fresh = makeUniqueNoThrow<cw::Puzzle>();
  if (!fresh) {
    LOG_ERR("CW", "OOM: puzzle");
    st.error = cw::Error::OutOfMemory;
    return st;
  }
  st = cw::store::loadSource(sourceKey, *fresh);
  if (st.ok()) adopt(std::move(fresh));
  return st;
}

void CrosswordActivity::adopt(std::unique_ptr<cw::Puzzle> fresh) {
  if (!fresh || !prog) return;
  clueListOnRelease = false;
  // The puzzle being left keeps its place.
  saveProgress();
  // Its saved progress, unless the source changed (or there is none): then fresh.
  if (!cw::store::loadProgress(fresh->fnv, *prog) || !cw::progressMatches(*fresh, *prog)) {
    cw::resetProgress(*fresh, *prog);
  }
  std::snprintf(prog->sourceKey, sizeof(prog->sourceKey), "%s", fresh->sourceKey);
  prog->seq = ++prefs.seq;
  std::snprintf(prefs.current, sizeof(prefs.current), "%s", fresh->sourceKey);
  if (!cw::store::savePrefs(prefs)) LOG_ERR("CW", "Prefs not saved");
  elapsedMs = (prog->elapsed > cw::ELAPSED_MAX ? cw::ELAPSED_MAX : prog->elapsed) * 1000u;
  lastTickMs = 0;
  bar = Bar::Clue;
  barWrong = 0;
  hasNext = false;
  contact = cw::Contact{};
  {
    RenderLock lock(*this);
    puzzle.swap(fresh);
    layout = cw::computeLayout(puzzle->w, puzzle->h);
    publish(false, true);  // the new puzzle's first frame is a HALF
    bannerShownMs = 0;
  }
  fresh.reset();  // the previous puzzle
  if (prog->solved) {
    hasNext = nextUnsolved(false);
    publish(false);
  }
  saveProgress();
  cw::store::pruneProgress(puzzle->fnv);
  LOG_INF("CW", "Playing %s (%ux%u, fnv %08lx)", puzzle->sourceKey, puzzle->w, puzzle->h,
          static_cast<unsigned long>(puzzle->fnv));
  requestUpdate();
}

bool CrosswordActivity::openFirstBuiltin() {
  cw::store::SolvedList solved;
  solved.load();
  const int count = static_cast<int>(cw::builtinCount());
  int first = cw::nextUnsolved(count, -1, builtinSolved, &solved);
  if (first < 0) first = 0;  // all solved: start at the beginning
  for (int k = 0; k < count; k++) {
    char key[cw::MAX_SOURCE_KEY + 1];
    if (builtinKey((first + k) % count, key, sizeof(key)) && openSource(key).ok()) return true;
  }
  return false;
}

bool CrosswordActivity::nextUnsolved(const bool open) {
  if (!puzzle) return false;
  char collection[cw::MAX_COLLECTION_KEY + 1];
  if (!cw::store::collectionOf(puzzle->sourceKey, collection, sizeof(collection))) return false;
  cw::store::SolvedList solved;
  solved.load();
  if (std::strcmp(collection, cw::COLLECTION_BUILTIN) == 0) {
    const int count = static_cast<int>(cw::builtinCount());
    const int current = cw::findBuiltin(cw::builtinIdFromKey(puzzle->sourceKey));
    const int next = cw::nextUnsolved(count, current, builtinSolved, &solved);
    if (next < 0 || next == current) return false;
    if (!open) return true;
    char key[cw::MAX_SOURCE_KEY + 1];
    return builtinKey(next, key, sizeof(key)) && openSource(key).ok();
  }
  auto files = makeUniqueNoThrow<cw::store::FileEntry[]>(cw::MAX_FOLDER_FILES);
  if (!files) {
    LOG_ERR("CW", "OOM: file list");
    return false;
  }
  const int count = cw::store::listFiles(collection, files.get(), cw::MAX_FOLDER_FILES);
  int current = -1;
  char key[cw::MAX_SOURCE_KEY + 1];
  for (int i = 0; i < count && current < 0; i++) {
    if (cw::store::fileKey(collection, files[i].name, key, sizeof(key)) && std::strcmp(key, puzzle->sourceKey) == 0) {
      current = i;
    }
  }
  CardCtx ctx{&solved, collection, files.get()};
  // Files that are refused are passed over; each is tried at most once.
  int from = current;
  for (int tries = 0; tries < count; tries++) {
    const int next = cw::nextUnsolved(count, from, cardSolved, &ctx);
    if (next < 0 || next == current) return false;
    if (!open) return true;
    if (cw::store::fileKey(collection, files[next].name, key, sizeof(key)) && openSource(key).ok()) return true;
    ctx.refused[next / 8] |= static_cast<uint8_t>(1u << (next % 8));
    from = next;
  }
  return false;
}

void CrosswordActivity::saveProgress() {
  if (failed || !puzzle || !prog || prog->fnv != puzzle->fnv) return;
  if (!prog->solved) prog->elapsed = elapsedMs / 1000;
  if (!cw::store::saveProgress(*prog)) LOG_ERR("CW", "Progress not saved");
}

void CrosswordActivity::publish(const bool pause, const bool half) {
  taskENTER_CRITICAL(&frameLock);
  shared->prog = *prog;
  shared->bar = bar;
  shared->wrong = barWrong;
  shared->hasNext = hasNext;
  if (pause) shared->pause = true;
  if (half) shared->half = true;
  taskEXIT_CRITICAL(&frameLock);
}

void CrosswordActivity::showBar(const Bar shown) {
  bar = shown;
  publish(false);
  requestUpdate();
}

void CrosswordActivity::onSolved() {
  prog->elapsed = elapsedMs / 1000;
  if (!cw::store::appendSolved(puzzle->fnv, puzzle->sourceKey)) LOG_ERR("CW", "Solved list not updated");
  hasNext = nextUnsolved(false);
  bannerShownMs = 0;
  LOG_INF("CW", "Solved %s in %lu s", puzzle->sourceKey, static_cast<unsigned long>(prog->elapsed));
}

void CrosswordActivity::apply(const cw::Change& change) {
  if (!change.changed && !change.solvedNow) return;
  lastInputMs = millis();
  idleHalfAsked = false;
  // "Not quite" stays until the next edit; "All solved here" until anything changes.
  if (bar == Bar::AllSolved || (change.edited && bar == Bar::NotQuite)) {
    bar = Bar::Clue;
    barWrong = 0;
  }
  if (change.fullWrong) {
    bar = Bar::NotQuite;
    barWrong = change.wrong;
  }
  if (change.solvedNow) onSolved();
  // The completion HALF travels with the solved frame: a frame already being drawn cannot take it.
  publish(change.wordChanged, change.solvedNow);
  // Saved at word boundaries and on completion (and by the callers of check, reveal, clear).
  if (change.wordChanged || change.solvedNow) saveProgress();
  requestUpdate();
}

void CrosswordActivity::pressKey(const int keyIndex) {
  const cw::Key& key = cw::keyboardKey(keyIndex);
  switch (key.kind) {
    case cw::KeyKind::Menu:
      openMenu();
      return;
    case cw::KeyKind::Del:
      apply(cw::deleteLetter(*puzzle, *prog));
      return;
    case cw::KeyKind::Letter:
      apply(cw::typeLetter(*puzzle, *prog, key.letter, prefs.skipFilled));
      return;
  }
}

void CrosswordActivity::handleTap(const cw::Target& target) {
  switch (target.kind) {
    case cw::TargetKind::Cell:
      apply(cw::tapCell(*puzzle, *prog, target.index));
      return;
    case cw::TargetKind::PrevClue:
      apply(cw::stepClue(*puzzle, *prog, -1));
      return;
    case cw::TargetKind::NextClue:
      apply(cw::stepClue(*puzzle, *prog, 1));
      return;
    case cw::TargetKind::ClueText:
      apply(cw::toggleDirection(*puzzle, *prog));
      return;
    case cw::TargetKind::Key:
      pressKey(target.index);
      return;
    case cw::TargetKind::BannerButton:
      // The banner's Next puzzle; when every remaining one is refused it says so instead.
      if (hasNext && !nextUnsolved(true)) {
        hasNext = false;
        publish(false);
        requestUpdate();
      }
      return;
    case cw::TargetKind::None:
      return;
  }
}

void CrosswordActivity::openMenu() {
  clueListOnRelease = false;  // a pending left-key open never outlives the screen change
  const uint32_t seconds = prog->solved ? prog->elapsed : elapsedMs / 1000;
  auto menu = makeUniqueNoThrow<CrosswordMenuActivity>(renderer, mappedInput, prefs, prog->solved, seconds,
                                                       prog->checks, prog->reveals);
  if (!menu) {
    LOG_ERR("CW", "OOM: menu");
    return;
  }
  contact = cw::Contact{};
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    lastTickMs = 0;  // the time in the menu does not count
    halfPending = true;
    const auto* menuResult = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !menuResult || !puzzle) return;
    using Menu = CrosswordMenuActivity;
    switch (menuResult->action) {
      case Menu::ROW_CLUE_LIST:
        openClueList();
        return;
      case Menu::ROW_CHECK_LETTER:
      case Menu::ROW_CHECK_WORD:
      case Menu::ROW_CHECK_PUZZLE:
        apply(cw::check(*puzzle, *prog, static_cast<cw::Scope>(menuResult->action - Menu::ROW_CHECK_LETTER)));
        saveProgress();
        return;
      case Menu::ROW_REVEAL_LETTER:
      case Menu::ROW_REVEAL_WORD:
      case Menu::ROW_REVEAL_PUZZLE:
        apply(cw::reveal(*puzzle, *prog, static_cast<cw::Scope>(menuResult->action - Menu::ROW_REVEAL_LETTER)));
        saveProgress();
        return;
      case Menu::ROW_CLEAR_WORD:
        apply(cw::clearWord(*puzzle, *prog));
        saveProgress();
        return;
      case Menu::ROW_CLEAR_PUZZLE:
        elapsedMs = 0;
        apply(cw::clearPuzzle(*puzzle, *prog));
        hasNext = false;
        publish(false);
        saveProgress();
        return;
      case Menu::ROW_PUZZLES:
        openPicker();
        return;
      case Menu::ROW_NEXT_UNSOLVED:
        if (!nextUnsolved(true)) showBar(Bar::AllSolved);
        return;
      default:
        return;
    }
  });
}

void CrosswordActivity::openClueList() {
  clueListOnRelease = false;
  auto list = makeUniqueNoThrow<CrosswordClueListActivity>(renderer, mappedInput, *puzzle, *prog);
  if (!list) {
    LOG_ERR("CW", "OOM: clue list");
    return;
  }
  contact = cw::Contact{};
  startActivityForResult(std::move(list), [this](const ActivityResult& result) {
    lastTickMs = 0;
    halfPending = true;
    const auto* chosen = std::get_if<MenuResult>(&result.data);
    if (!chosen || !puzzle || chosen->action < 0 || chosen->action >= puzzle->entryCount) return;
    apply(cw::gotoEntry(*puzzle, *prog, chosen->action));
  });
}

void CrosswordActivity::openPicker() {
  clueListOnRelease = false;
  auto picker = makeUniqueNoThrow<CrosswordPickerActivity>(renderer, mappedInput, prefs, staged, puzzle->sourceKey);
  if (!picker) {
    LOG_ERR("CW", "OOM: picker");
    return;
  }
  contact = cw::Contact{};
  startActivityForResult(std::move(picker), [this](const ActivityResult&) {
    lastTickMs = 0;
    halfPending = true;
    if (staged) adopt(std::move(staged));
  });
}

void CrosswordActivity::exitToApps() {
  AppsActivity::selectOnNextOpen(AppsActivity::App::Crossword);
  activityManager.goToApps();
}

bool CrosswordActivity::bannerArmed(const uint32_t contactDownMs) const {
  const uint32_t shown = bannerShownMs.load();
  if (shown == 0) return false;
  // A key press (0) needs only the banner drawn; a contact must also begin after it.
  return contactDownMs == 0 || static_cast<int32_t>(contactDownMs - shown) >= 0;
}

void CrosswordActivity::handleKeys() {
  if (BoardConfig::isX4Pro()) {
    // Two edge keys: a long press acts (it swallows its own release), a short one steps.
    if (mappedInput.wasLongPressed(Button::Down, KEY_LONG_PRESS_MS)) {
      if (prog->solved) {
        if (bannerArmed(0)) handleTap(cw::Target{cw::TargetKind::BannerButton, -1});
      } else {
        apply(cw::toggleDirection(*puzzle, *prog));
      }
      return;
    }
    // The clue list opens once the key is let go: opened at the threshold, the still-held key
    // reached the list's hold-to-repeat and moved its selection off the current clue.
    if (mappedInput.wasLongPressed(Button::Up, KEY_LONG_PRESS_MS)) {
      clueListOnRelease = true;
      return;
    }
    if (clueListOnRelease) {
      if (mappedInput.isPressed(Button::Up)) return;
      clueListOnRelease = false;
      openClueList();
      return;
    }
    if (mappedInput.wasReleased(Button::Down)) {
      apply(cw::stepClue(*puzzle, *prog, 1));
    } else if (mappedInput.wasReleased(Button::Up)) {
      apply(cw::stepClue(*puzzle, *prog, -1));
    }
    return;
  }
  // Front buttons (a touch board without the edge keys): hold Confirm for the menu.
  if (mappedInput.wasLongPressed(Button::Confirm, KEY_LONG_PRESS_MS)) {
    openMenu();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (prog->solved) {
      if (bannerArmed(0)) handleTap(cw::Target{cw::TargetKind::BannerButton, -1});
    } else {
      apply(cw::toggleDirection(*puzzle, *prog));
    }
    return;
  }
  const auto step = [this](const int delta) { apply(cw::stepClue(*puzzle, *prog, delta)); };
  buttonNavigator.onPressAndContinuous({Button::ScreenLeft, Button::ScreenUp}, [&step] { step(-1); });
  buttonNavigator.onPressAndContinuous({Button::ScreenRight, Button::ScreenDown}, [&step] { step(1); });
}

void CrosswordActivity::tickElapsed(const unsigned long now) {
  if (lastTickMs != 0 && !prog->solved) {
    const unsigned long step = now - lastTickMs;
    elapsedMs += step > ELAPSED_STEP_MAX_MS ? ELAPSED_STEP_MAX_MS : step;
    if (elapsedMs > ELAPSED_MAX_MS) elapsedMs = ELAPSED_MAX_MS;
  }
  lastTickMs = now != 0 ? now : 1;
}

void CrosswordActivity::checkIdleRefresh(const unsigned long now) {
  // The counter ran out mid-word: the HALF comes once the player pauses.
  if (idleHalfAsked || lastInputMs == 0 || now - lastInputMs < IDLE_HALF_MS || rendersUntilHalf.load() > 1) return;
  idleHalfAsked = true;
  publish(true);
  requestUpdate();
}

void CrosswordActivity::loop() {
  if (failed || !puzzle) {
    if (mappedInput.wasReleased(Button::Back)) exitToApps();
    return;
  }
  const unsigned long now = millis();
  tickElapsed(now);

  // Touch first: a tap's end must be taken before Back is read.
  int x = 0;
  int y = 0;
  const bool held = mappedInput.isScreenTouchHeld(x, y);
  const bool released = mappedInput.wasScreenTouchReleased();
  // The late release of a tap that ended early (a second finger) is the tap's too.
  const bool lateRelease = released && releasePending;
  if (released) releasePending = false;
  const cw::ContactEvent event =
      cw::trackContact(contact, layout, prog->solved, held, x, y, released, static_cast<uint32_t>(now));
  switch (event.kind) {
    case cw::ContactEvent::Kind::Began:
      lastInputMs = now;
      idleHalfAsked = false;
      break;
    case cw::ContactEvent::Kind::Hold:
      apply(cw::clearWord(*puzzle, *prog));
      saveProgress();
      return;
    case cw::ContactEvent::Kind::Tap:
      if (!released) releasePending = true;
      if (event.target.kind == cw::TargetKind::BannerButton && !bannerArmed(contact.downMs)) return;
      handleTap(event.target);
      return;
    case cw::ContactEvent::Kind::None:
      break;
  }
  // A contact that began on a game target is the game's even when it did not tap (a slide off a
  // key, a drag across the grid): its end is never also the left-edge Back swipe. Swipes that
  // start off every target (header, margins) still reach Back.
  const bool ownEnd = event.ended && contact.start.kind != cw::TargetKind::None;
  if (ownEnd && !released) releasePending = true;
  if (contact.active || ownEnd || lateRelease) return;

  // Home long press (its default action, Reader Menu, means nothing outside the reader).
  if (mappedInput.homeButtonAction() == HomeButtonAction::ReaderMenu) {
    openMenu();
    return;
  }
  // The header arrow, the left-edge swipe, or a Back key.
  if (!releasePending && mappedInput.wasReleased(Button::Back)) {
    exitToApps();
    return;
  }
  handleKeys();
  checkIdleRefresh(now);
}

bool CrosswordActivity::handleForcedRefresh() {
  halfPending = true;
  requestUpdate();
  return true;
}

void CrosswordActivity::render(RenderLock&&) {
  if (!ready.load()) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight};
  renderer.clearScreen();
  if (failed || !puzzle || !drawn) {
    GUI.drawHeader(renderer, header, tr(STR_CROSSWORD));
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_MEMORY_ERROR));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  const unsigned long start = millis();

  // What loop() changed, copied once.
  taskENTER_CRITICAL(&frameLock);
  *drawn = *shared;
  shared->pause = false;
  shared->half = false;
  taskEXIT_CRITICAL(&frameLock);
  const cw::Progress& p = drawn->prog;

  cw::draw::View view;
  view.prog = &p;
  const int entry = cw::currentEntry(*puzzle, p);
  cw::formatClueLabel(*puzzle, entry, clueLabel, sizeof(clueLabel));
  view.clueLabel = clueLabel;
  view.clueText = puzzle->clue(entry);  // content
  if (drawn->bar == Bar::NotQuite) {
    if (drawn->wrong == 1) {
      std::snprintf(barText, sizeof(barText), "%s", tr(STR_CW_NOT_QUITE_ONE));
    } else {
      std::snprintf(barText, sizeof(barText), tr(STR_CW_NOT_QUITE), drawn->wrong);
    }
    view.barMessage = barText;
  } else if (drawn->bar == Bar::AllSolved) {
    view.barMessage = tr(STR_CW_ALL_SOLVED);
  }
  view.menuLabel = tr(STR_CW_MENU);
  view.delLabel = tr(STR_CW_DEL);
  if (p.solved) {
    char time[16];
    cw::formatElapsed(p.elapsed, time, sizeof(time));
    std::snprintf(bannerTitle, sizeof(bannerTitle), tr(STR_CW_SOLVED_IN), time);
    char checks[24] = {};
    char reveals[24] = {};
    if (p.checks == 1) std::snprintf(checks, sizeof(checks), "%s", tr(STR_CW_ONE_CHECK));
    if (p.checks > 1) std::snprintf(checks, sizeof(checks), tr(STR_CW_CHECKS), p.checks);
    if (p.reveals == 1) std::snprintf(reveals, sizeof(reveals), "%s", tr(STR_CW_ONE_REVEAL));
    if (p.reveals > 1) std::snprintf(reveals, sizeof(reveals), tr(STR_CW_REVEALS), p.reveals);
    std::snprintf(bannerDetail, sizeof(bannerDetail), "%s%s%s", checks, checks[0] && reveals[0] ? " - " : "", reveals);
    view.bannerTitle = bannerTitle;
    view.bannerDetail = bannerDetail;
    view.bannerButton = drawn->hasNext ? tr(STR_CW_NEXT_PUZZLE) : "";
    view.bannerNote = drawn->hasNext ? "" : tr(STR_CW_ALL_SOLVED);
  }

  // The title is content (built-in English, or the file's own).
  GUI.drawHeader(renderer, header, puzzle->title[0] ? puzzle->title : tr(STR_CROSSWORD));
  cw::draw::drawScreen(renderer, *puzzle, layout, view);

  // FAST per change. HALF for a new or loaded puzzle, the return from the menu, the clue list or
  // the picker and a forced refresh (halfPending), a new puzzle and completion (carried by their
  // frame, so an earlier frame cannot take it); and when the reader's counter has
  // run out, but then only at a pause point (another word, or idle): never mid-word.
  auto mode = HalDisplay::FAST_REFRESH;
  if (halfPending.exchange(false) || drawn->half) {
    mode = HalDisplay::HALF_REFRESH;
    rendersUntilHalf = SETTINGS.getRefreshFrequency();
  } else if (rendersUntilHalf.load() <= 1 && drawn->pause) {
    mode = HalDisplay::HALF_REFRESH;
    rendersUntilHalf = SETTINGS.getRefreshFrequency();
  } else if (rendersUntilHalf.load() > 1) {
    rendersUntilHalf--;
  }
  LOG_DBG("CW", "Drawn in %lu ms (%s)", millis() - start, mode == HalDisplay::HALF_REFRESH ? "half" : "fast");
  renderer.displayBuffer(mode);
  // The banner's button counts only for contacts that begin once the banner is on the panel: a
  // letter typed ahead of the solving frame must not become "Next puzzle".
  if (!p.solved) {
    bannerShownMs = 0;
  } else if (bannerShownMs.load() == 0) {
    const uint32_t shown = static_cast<uint32_t>(millis());
    bannerShownMs = shown != 0 ? shown : 1;
  }
}

#if CROSSPOINT_BENCH_CONSOLE
cw::LoadStatus CrosswordActivity::benchOpen(const char* sourceKey) {
  if (failed) {
    cw::LoadStatus st;
    st.error = cw::Error::OutOfMemory;
    return st;
  }
  return openSource(sourceKey);
}

void CrosswordActivity::benchType(const char* keys) {
  if (failed || !puzzle) return;
  for (const char* c = keys; c && *c; c++) {
    if (*c == '-') {
      pressKey(cw::delKey());
      continue;
    }
    const int key = cw::keyForLetter(*c);
    if (key >= 0) pressKey(key);
  }
}

bool CrosswordActivity::benchCursor(const int row, const int col, const int dir) {
  if (failed || !puzzle || row < 0 || col < 0 || row >= puzzle->h || col >= puzzle->w) return false;
  const int cell = puzzle->index(row, col);
  if (puzzle->isBlock(cell)) return false;
  apply(cw::setCursor(*puzzle, *prog, cell, dir < 0 ? prog->dir : static_cast<uint8_t>(dir)));
  return prog->cursor == cell;
}

void CrosswordActivity::benchCheck(const cw::Scope scope) {
  if (failed || !puzzle) return;
  apply(cw::check(*puzzle, *prog, scope));
  saveProgress();
}

void CrosswordActivity::benchReveal(const cw::Scope scope) {
  if (failed || !puzzle) return;
  apply(cw::reveal(*puzzle, *prog, scope));
  saveProgress();
}

void CrosswordActivity::benchSolve() {
  if (failed || !puzzle) return;
  for (int cell = 0; cell < puzzle->cells() && !prog->solved; cell++) {
    if (puzzle->isBlock(cell) || prog->fill[cell] == puzzle->solution[cell]) continue;
    if (prog->flags[cell] & cw::FLAG_REVEALED) continue;
    apply(cw::setCursor(*puzzle, *prog, cell, prog->dir));
    const int key = cw::keyForLetter(puzzle->solution[cell]);
    if (prog->cursor == cell && key >= 0) pressKey(key);
  }
}
#endif
