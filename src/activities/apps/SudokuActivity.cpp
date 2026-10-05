#include "SudokuActivity.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "AppsActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SudokuMenuActivity.h"
#include "SudokuStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

using Button = MappedInputManager::Button;

constexpr unsigned long KEY_LONG_PRESS_MS = 600;
// One loop pass that took longer (a card write, a screen over the game) counts as this much.
constexpr unsigned long ELAPSED_STEP_MAX_MS = 1000;
constexpr uint32_t ELAPSED_MAX_MS = sd::ELAPSED_MAX * 1000u;
// The pause: the game is saved, and a HALF refresh owed by the counter is drawn.
constexpr unsigned long IDLE_MS = 2000;

// Started on: an entry or a note (New puzzle then asks again).
bool inProgress(const sd::Game& g) {
  if (g.solved) return false;
  for (int i = 0; i < sd::CELLS; i++) {
    if (g.value[i] != g.givens[i] || g.notes[i]) return true;
  }
  return false;
}

// The unit (row, then column, then box) where the square's digit shows twice; false for none.
bool clashUnitOf(const sd::Game& g, const int cell, uint8_t& unit) {
  const uint8_t v = g.value[cell];
  if (!v) return false;
  const int units[3] = {sd::rowOf(cell), 9 + sd::colOf(cell), 18 + sd::boxOf(cell)};
  for (const int u : units) {
    int same = 0;
    for (int k = 0; k < 9; k++) same += g.value[sd::unitCell(u, k)] == v;
    if (same > 1) {
      unit = static_cast<uint8_t>(u);
      return true;
    }
  }
  return false;
}

}  // namespace

SudokuActivity::SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(NAME, renderer, mappedInput) {}

void SudokuActivity::onEnter() {
  Activity::onEnter();
  model = makeUniqueNoThrow<sd::Model>();
  shared = makeUniqueNoThrow<Frame>();
  drawn = makeUniqueNoThrow<Frame>();
  if (!model || !shared || !drawn) {
    LOG_ERR("SU", "OOM: game");
    failed = true;
  } else {
    sd::store::loadPrefs(prefs);
    // The puzzle in progress; on the first run, or when the save is gone or damaged, the next
    // numbered puzzle of the prefs' tier (Easy 1 at first).
    if (sd::store::loadGame(model->game)) {
      model->resetSession();
      model->settings.removeNotes = prefs.removeNotes;
      elapsedMs = model->game.elapsed * 1000u;
      publish(false, true);
      LOG_INF("SU", "Resumed %s %lu (fnv %08lx)", sd::tierKey(model->game.tier),
              static_cast<unsigned long>(model->game.number), static_cast<unsigned long>(model->game.fnv));
    } else if (!newPuzzle()) {
      failed = true;
    }
  }
  halfPending = true;
  ready.store(true);
  requestUpdate();
}

void SudokuActivity::onExit() {
  // ActivityManager holds the render lock here, so nothing reads the frames meanwhile.
  save();
  model.reset();
  shared.reset();
  drawn.reset();
  Activity::onExit();
}

bool SudokuActivity::startPuzzle(const int tier, const uint32_t number, const uint32_t seed) {
  if (!model) return false;
  sd::Generated gen;
  {
    // The full clock: the idle and refresh-wait downclocks would triple the time.
    HalPowerManager::Lock power;
    const unsigned long start = millis();
    if (!sd::generate(seed, tier, gen)) {
      LOG_ERR("SU", "No puzzle from seed %08lx", static_cast<unsigned long>(seed));
      return false;
    }
    LOG_INF("SU", "Generated %s %lu in %lu ms at %lu MHz (%u tries, %d givens, %s%s)", sd::tierKey(tier),
            static_cast<unsigned long>(number), millis() - start, static_cast<unsigned long>(getCpuFrequencyMhz()),
            static_cast<unsigned>(gen.tries), gen.givenCount(), sd::tierKey(gen.tier),
            gen.exact ? "" : ", out of tries");
  }
  sd::startGame(model->game, gen, number, seed);
  model->resetSession();
  model->settings.removeNotes = prefs.removeNotes;
  note = sd::draw::Note{};
  contact = sd::Contact{};
  menuOnRelease = false;
  elapsedMs = 0;
  lastTickMs = 0;
  bannerShownMs = 0;
  dirty = true;
  publish(false, true);  // the new puzzle's first frame is a HALF
  save();
  requestUpdate();
  return true;
}

bool SudokuActivity::newPuzzle() {
  const int tier = prefs.tier < sd::TIER_COUNT ? prefs.tier : sd::Easy;
  const uint32_t number = sd::takeNumber(prefs, tier);
  if (!sd::store::savePrefs(prefs)) LOG_ERR("SU", "Prefs not saved");
  return startPuzzle(tier, number, sd::puzzleSeed(tier, number));
}

void SudokuActivity::save() {
  if (failed || !model) return;
  sd::Game& g = model->game;
  if (!g.solved) g.elapsed = (elapsedMs > ELAPSED_MAX_MS ? ELAPSED_MAX_MS : elapsedMs) / 1000;
  if (sd::store::saveGame(g)) {
    dirty = false;
  } else {
    LOG_ERR("SU", "Puzzle not saved");
  }
}

void SudokuActivity::publish(const bool pause, const bool half) {
  taskENTER_CRITICAL(&frameLock);
  shared->game = model->game;
  shared->note = note;
  if (pause) shared->pause = true;
  if (half) shared->half = true;
  taskEXIT_CRITICAL(&frameLock);
}

void SudokuActivity::onSolved() {
  sd::Game& g = model->game;
  g.elapsed = (elapsedMs > ELAPSED_MAX_MS ? ELAPSED_MAX_MS : elapsedMs) / 1000;
  if (!sd::store::appendSolved(g)) LOG_ERR("SU", "Solved list not updated");
  bannerShownMs = 0;
  LOG_INF("SU", "Solved %s %lu in %lu s (%u hints, %u checks, %u reveals)", sd::tierKey(g.tier),
          static_cast<unsigned long>(g.number), static_cast<unsigned long>(g.elapsed), g.hints, g.checks, g.reveals);
}

void SudokuActivity::apply(const sd::Change& change, const int cell) {
  if (!change.changed && !change.solvedNow) return;
  lastInputMs = millis();
  idleDone = false;
  // A message lasts until the next change.
  note = sd::draw::Note{};
  if (change.msg != sd::Msg::None) {
    note.msg = change.msg;
    note.digit = change.digit;
    note.count = change.count;
    note.hint = change.hint;
  } else if (change.edited && cell >= 0 && cell < sd::CELLS && sd::isClash(model->game, cell)) {
    uint8_t unit = sd::NO_UNIT;
    if (clashUnitOf(model->game, cell, unit)) {
      note.clashDigit = model->game.value[cell];
      note.clashUnit = unit;
    }
  }
  if (change.edited) dirty = true;
  if (change.solvedNow) onSolved();
  // The completion HALF travels with the solved frame: a frame already being drawn cannot take it.
  publish(change.modeChanged, change.solvedNow);
  if (change.solvedNow) save();
  requestUpdate();
}

void SudokuActivity::handleTap(const sd::Target& target, const uint32_t now) {
  switch (target.kind) {
    case sd::TargetKind::Cell:
      apply(sd::tapCell(*model, target.index, now), target.index);
      return;
    case sd::TargetKind::Digit: {
      const int at = model->game.cursor;
      apply(sd::tapDigit(*model, target.index, now), model->game.lock == sd::LOCK_NONE ? at : -1);
      return;
    }
    case sd::TargetKind::Tool:
      switch (static_cast<sd::Tool>(target.index)) {
        case sd::Tool::Notes:
          apply(sd::toggleNotesMode(*model, now));
          return;
        case sd::Tool::Erase:
          apply(sd::tapErase(*model, now));
          return;
        case sd::Tool::Undo:
          apply(sd::undo(*model));
          return;
        case sd::Tool::Menu:
          openMenu();
          return;
      }
      return;
    case sd::TargetKind::BannerButton:
      newPuzzle();
      return;
    case sd::TargetKind::None:
      return;
  }
}

void SudokuActivity::handleHold(const sd::Target& target, const uint32_t now) {
  if (target.kind == sd::TargetKind::Digit) {
    apply(sd::holdDigit(*model, target.index, now));
  } else if (target.kind == sd::TargetKind::Tool && static_cast<sd::Tool>(target.index) == sd::Tool::Erase) {
    apply(sd::holdErase(*model, now));
  }
}

void SudokuActivity::openMenu() {
  menuOnRelease = false;  // a pending left-key open never outlives the screen change
  save();
  const sd::Game& g = model->game;
  const uint32_t seconds = g.solved ? g.elapsed : elapsedMs / 1000;
  auto menu = makeUniqueNoThrow<SudokuMenuActivity>(renderer, mappedInput, prefs, g, inProgress(g), seconds);
  if (!menu) {
    LOG_ERR("SU", "OOM: menu");
    return;
  }
  contact = sd::Contact{};
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    lastTickMs = 0;  // the time in the menu does not count
    halfPending = true;
    if (!model) return;
    model->settings.removeNotes = prefs.removeNotes;
    const auto* menuResult = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !menuResult) return;
    using Menu = SudokuMenuActivity;
    switch (menuResult->action) {
      case Menu::ROW_HINT:
        apply(sd::hint(*model));
        return;
      case Menu::ROW_CHECK_SQUARE:
      case Menu::ROW_CHECK_PUZZLE:
        apply(sd::check(*model, menuResult->action == Menu::ROW_CHECK_SQUARE ? sd::Scope::Square : sd::Scope::Puzzle));
        save();
        return;
      case Menu::ROW_REVEAL_SQUARE:
      case Menu::ROW_REVEAL_PUZZLE:
        apply(
            sd::reveal(*model, menuResult->action == Menu::ROW_REVEAL_SQUARE ? sd::Scope::Square : sd::Scope::Puzzle));
        save();
        return;
      case Menu::ROW_FILL_NOTES:
        apply(sd::fillAllNotes(*model));
        return;
      case Menu::ROW_NEW_PUZZLE:
        // Generated here, before the frame the return asks for.
        newPuzzle();
        return;
      default:
        return;
    }
  });
}

void SudokuActivity::exitToApps() {
  AppsActivity::selectOnNextOpen(AppsActivity::App::Sudoku);
  activityManager.goToApps();
}

bool SudokuActivity::bannerArmed(const uint32_t contactDownMs) const {
  const uint32_t shown = bannerShownMs.load();
  if (shown == 0) return false;
  // A key press (0) needs only the banner drawn; a contact must also begin after it.
  return contactDownMs == 0 || static_cast<int32_t>(contactDownMs - shown) >= 0;
}

void SudokuActivity::handleKeys(const uint32_t now) {
  const bool solved = model->game.solved;
  if (BoardConfig::isX4Pro()) {
    // Two edge keys: a long press acts (it swallows its own release), a short one steps.
    if (mappedInput.wasLongPressed(Button::Down, KEY_LONG_PRESS_MS)) {
      if (solved) {
        if (bannerArmed(0)) newPuzzle();
      } else {
        apply(sd::hint(*model));
      }
      return;
    }
    // The menu opens once the key is let go: opened at the threshold, the still-held key would
    // reach the menu's list.
    if (mappedInput.wasLongPressed(Button::Up, KEY_LONG_PRESS_MS)) {
      menuOnRelease = true;
      return;
    }
    if (menuOnRelease) {
      if (mappedInput.isPressed(Button::Up)) return;
      menuOnRelease = false;
      openMenu();
      return;
    }
    if (mappedInput.wasReleased(Button::Down)) {
      apply(sd::toggleNotesMode(*model, now));
    } else if (mappedInput.wasReleased(Button::Up)) {
      apply(sd::undo(*model));
    }
    return;
  }
  // Front buttons (a touch board without the edge keys): hold Confirm for the menu.
  if (mappedInput.wasLongPressed(Button::Confirm, KEY_LONG_PRESS_MS)) {
    openMenu();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (solved) {
      if (bannerArmed(0)) newPuzzle();
    } else {
      apply(sd::toggleNotesMode(*model, now));
    }
  }
}

void SudokuActivity::tickElapsed(const unsigned long now) {
  if (lastTickMs != 0 && !model->game.solved) {
    const unsigned long step = now - lastTickMs;
    elapsedMs += step > ELAPSED_STEP_MAX_MS ? ELAPSED_STEP_MAX_MS : step;
    if (elapsedMs > ELAPSED_MAX_MS) elapsedMs = ELAPSED_MAX_MS;
  }
  lastTickMs = now != 0 ? now : 1;
}

void SudokuActivity::checkIdle(const unsigned long now) {
  if (idleDone || lastInputMs == 0 || now - lastInputMs < IDLE_MS) return;
  idleDone = true;
  if (dirty) save();
  // The counter ran out mid-play: the HALF comes now the player has paused.
  if (rendersUntilHalf.load() <= 1) {
    publish(true);
    requestUpdate();
  }
}

void SudokuActivity::loop() {
  if (failed || !model) {
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
  const sd::ContactEvent event =
      sd::trackContact(contact, model->game.solved, held, x, y, released, static_cast<uint32_t>(now));
  switch (event.kind) {
    case sd::ContactEvent::Kind::Began:
      lastInputMs = now;
      idleDone = false;
      break;
    case sd::ContactEvent::Kind::Hold:
      handleHold(event.target, static_cast<uint32_t>(now));
      return;
    case sd::ContactEvent::Kind::Tap:
      if (!released) releasePending = true;
      if (event.target.kind == sd::TargetKind::BannerButton && !bannerArmed(contact.downMs)) return;
      handleTap(event.target, static_cast<uint32_t>(now));
      return;
    case sd::ContactEvent::Kind::None:
      break;
  }
  // A contact that began on a game target is the game's even when it did not tap (a slide off a
  // key, a drag across the grid): its end is never also the left-edge Back swipe. Swipes that
  // start off every target (the bezel, the header, the status line) still reach Back.
  const bool ownEnd = event.ended && contact.start.kind != sd::TargetKind::None;
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
  handleKeys(static_cast<uint32_t>(now));
  checkIdle(now);
}

bool SudokuActivity::handleForcedRefresh() {
  halfPending = true;
  requestUpdate();
  return true;
}

void SudokuActivity::render(RenderLock&&) {
  if (!ready.load()) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight};
  renderer.clearScreen();
  GUI.drawHeader(renderer, header, tr(STR_SUDOKU));
  if (failed || !drawn || !shared) {
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
  const sd::Game& g = drawn->game;

  sd::draw::View view;
  view.game = &g;
  view.statusMessage = sd::draw::formatStatus(g, drawn->note, statusText, sizeof(statusText));
  view.status = statusText;
  view.toolLabels[static_cast<int>(sd::Tool::Notes)] = tr(STR_SD_NOTES);
  view.toolLabels[static_cast<int>(sd::Tool::Erase)] = tr(STR_SD_ERASE);
  view.toolLabels[static_cast<int>(sd::Tool::Undo)] = tr(STR_SD_UNDO);
  view.toolLabels[static_cast<int>(sd::Tool::Menu)] = tr(STR_CW_MENU);
  if (g.solved) {
    sd::draw::formatBanner(g, bannerTitle, sizeof(bannerTitle), bannerDetail, sizeof(bannerDetail));
    view.bannerTitle = bannerTitle;
    view.bannerDetail = bannerDetail;
    view.bannerButton = tr(STR_WS_NEW_PUZZLE);
  }
  sd::draw::drawScreen(renderer, view);

  // FAST per change. HALF on entry, the return from the menu and a forced refresh (halfPending), a
  // new puzzle and completion (carried by their frame, so an earlier frame cannot take it); and
  // when the reader's counter has run out, but then only at a pause (idle, Notes, a lock).
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
  LOG_DBG("SU", "Drawn in %lu ms (%s)", millis() - start, mode == HalDisplay::HALF_REFRESH ? "half" : "fast");
  renderer.displayBuffer(mode);
  // The banner's button counts only for contacts that begin once the banner is on the panel: a
  // digit tapped ahead of the solving frame must not become "New puzzle".
  if (!g.solved) {
    bannerShownMs = 0;
  } else if (bannerShownMs.load() == 0) {
    const uint32_t shown = static_cast<uint32_t>(millis());
    bannerShownMs = shown != 0 ? shown : 1;
  }
}

#if CROSSPOINT_BENCH_CONSOLE
bool SudokuActivity::benchStart(const int tier, const uint32_t number, const uint32_t seed) {
  return !failed && startPuzzle(tier, number, seed);
}

void SudokuActivity::benchPut(const int cell, const int digit) {
  if (failed || !model) return;
  apply(sd::putDigit(*model, cell, digit), cell);
}

void SudokuActivity::benchNote(const int cell, const int digit) {
  if (failed || !model) return;
  apply(sd::toggleNote(*model, cell, digit));
}

void SudokuActivity::benchErase(const int cell) {
  if (failed || !model) return;
  apply(sd::eraseSquare(*model, cell));
}

void SudokuActivity::benchHint() {
  if (failed || !model) return;
  apply(sd::hint(*model));
}

void SudokuActivity::benchCheck(const sd::Scope scope) {
  if (failed || !model) return;
  apply(sd::check(*model, scope));
  save();
}

void SudokuActivity::benchReveal(const sd::Scope scope) {
  if (failed || !model) return;
  apply(sd::reveal(*model, scope));
  save();
}

void SudokuActivity::benchSolve() {
  if (failed || !model) return;
  const sd::Game& g = model->game;
  for (int cell = 0; cell < sd::CELLS && !g.solved; cell++) {
    if (g.value[cell] == g.solution[cell] || sd::isLocked(g, cell)) continue;
    apply(sd::putDigit(*model, cell, g.solution[cell]), cell);
  }
}
#endif
