#include "GuideApp.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <string>

#include "AppsActivity.h"
#include "CrossPointSettings.h"
#include "GuideDraw.h"
#include "GuideListActivity.h"
#include "GuidePageActivity.h"
#include "GuideStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"

namespace {

using Button = MappedInputManager::Button;
using gd::store::Store;

constexpr unsigned long KEY_LONG_PRESS_MS = 600;
constexpr unsigned long IDLE_SAVE_MS = 3000;

// A guide screen is replacing another: the one leaving keeps the store open (loop task only).
bool transferring = false;
// The refresh counter runs across the guide's screens (a page turn into the next topic is a new
// screen): render task only. 0 = not started (the first frame is a HALF anyway).
int rendersUntilHalf = 0;
int figureScreens = 0;  // figure screens drawn FAST since the last HALF

bool sameState(const gd::State& a, const gd::State& b) {
  return a.screen == b.screen && a.list == b.list && std::strcmp(a.category, b.category) == 0 &&
         std::strcmp(a.query, b.query) == 0 && std::strcmp(a.topic, b.topic) == 0 && a.page == b.page &&
         a.sub == b.sub && a.sel == b.sel;
}

std::unique_ptr<Activity> makeScreen(GfxRenderer& renderer, MappedInputManager& mappedInput, gd::State s,
                                     const bool fast) {
  Store& store = Store::get();
  if (store.open() == gd::PackError::None) {
    gd::sanitizeState(s, store.catalog());
  } else {
    s = gd::State{};  // the home says what is wrong
  }
  if (s.screen == gd::Screen::Page || s.screen == gd::Screen::About) {
    return makeUniqueNoThrow<GuidePageActivity>(renderer, mappedInput, s, fast);
  }
  return makeUniqueNoThrow<GuideListActivity>(renderer, mappedInput, s, fast);
}

}  // namespace

std::unique_ptr<Activity> GuideScreen::makeEntry(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const bool resume) {
  gd::State s;
  Store& store = Store::get();
  if (resume && store.open() == gd::PackError::None && !store.loadState(s)) s = gd::State{};
  if (!resume) s = gd::State{};
  auto activity = makeScreen(renderer, mappedInput, s, false);
  if (!activity) LOG_ERR("GD", "OOM: guide screen");
  return activity;
}

bool GuideScreen::openScreen(const gd::State& state, const bool fast) {
  auto activity = makeScreen(renderer, mappedInput, state, fast);
  if (!activity) {
    LOG_ERR("GD", "OOM: guide screen");
    return false;
  }
  // No save here: the incoming screen's state supersedes this one's, and it saves it itself.
  transferring = true;
  activityManager.replaceActivity(std::move(activity));
  return true;
}

void GuideScreen::exitToApps() {
  AppsActivity::selectOnNextOpen(AppsActivity::App::Guide);
  activityManager.goToApps();
}

GuideScreen::GuideScreen(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                         const gd::State& state)
    : Activity(name, renderer, mappedInput), st(state) {}

void GuideScreen::onEnter() {
  Activity::onEnter();
  transferring = false;
  // This screen's state is new (a page turn's latency is not spent on the card): saved once input
  // pauses, or when the guide is left.
  noteChange();
}

void GuideScreen::onExit() {
  // Leaving the guide (Apps, Home, sleep): save, then the pack's memory goes with it. A change of
  // guide screen saves nothing (the incoming screen's state supersedes this one's; it saves it, and
  // the recent list, once input pauses or when it exits).
  if (!transferring) {
    saveState();
    Store::get().release();
  }
  transferring = false;
  Activity::onExit();
}

bool GuideScreen::handleForcedRefresh() {
  halfPending = true;
  requestUpdate();
  return true;
}

void GuideScreen::noteChange() { lastChangeMs = millis() | 1u; }

void GuideScreen::tickIdleSave(const unsigned long now) {
  if (lastChangeMs == 0 || now - lastChangeMs < IDLE_SAVE_MS) return;
  lastChangeMs = 0;
  saveState();
}

void GuideScreen::saveState(const bool force) {
  Store& store = Store::get();
  if (!store.isOpen()) return;
  if (!store.flushRecent()) LOG_ERR("GD", "Recent not saved");
  if (!force && everSaved && sameState(st, saved)) return;
  if (store.saveState(st)) {
    saved = st;
    everSaved = true;
  } else {
    LOG_ERR("GD", "State not saved");
  }
}

// ---- keys -----------------------------------------------------------------------------------------------

GuideScreen::Keys GuideScreen::readKeys() {
  Keys k;
  if (BoardConfig::isX4Pro()) {
    if (mappedInput.wasLongPressed(Button::Down, KEY_LONG_PRESS_MS)) {
      k.actLong = true;
      return k;
    }
    if (mappedInput.wasLongPressed(Button::Up, KEY_LONG_PRESS_MS)) {
      upOnRelease = true;
      return k;
    }
    if (upOnRelease) {
      if (mappedInput.isPressed(Button::Up)) return k;
      upOnRelease = false;
      k.upLong = true;
      return k;
    }
    if (mappedInput.wasReleased(Button::Down)) {
      k.step = 1;
    } else if (mappedInput.wasReleased(Button::Up)) {
      k.step = -1;
    }
    return k;
  }
  // Front buttons (a touch board without the edge keys).
  if (mappedInput.wasLongPressed(Button::Confirm, KEY_LONG_PRESS_MS)) {
    k.actLong = true;
    return k;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    k.confirm = true;
  } else if (mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down) ||
             mappedInput.wasReleased(Button::PageForward)) {
    k.step = 1;
  } else if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up) ||
             mappedInput.wasReleased(Button::PageBack)) {
    k.step = -1;
  }
  return k;
}

// ---- the guide menu ----------------------------------------------------------------------------------

void GuideScreen::openMenu() {
  menu.open = true;
  menu.selected = 0;
  upOnRelease = false;
  publish();
}

void GuideScreen::closeMenu() {
  menu.open = false;
  publish();
}

bool GuideScreen::handleMenuInput() {
  if (!menu.open) {
    // Home held (its default action, Reader Menu, means nothing outside the reader).
    if (mappedInput.homeButtonAction() == HomeButtonAction::ReaderMenu) {
      openMenu();
      return true;
    }
    return false;
  }
  if (mappedInput.homeButtonAction() == HomeButtonAction::ReaderMenu) {
    closeMenu();
    return true;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const int row = gd::draw::menuRowAt(MENU_COUNT, x, y);
    if (row >= 0) {
      runMenuItem(row);
    } else if (row == -2) {
      closeMenu();
    }
    return true;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    closeMenu();
    return true;
  }
  const Keys k = readKeys();
  if (k.step != 0) {
    menu.selected = static_cast<int8_t>((menu.selected + k.step + MENU_COUNT) % MENU_COUNT);
    publish();
  } else if (k.actLong || k.confirm) {
    runMenuItem(menu.selected);
  } else if (k.upLong) {
    closeMenu();
  }
  return true;  // nothing beneath the menu takes input while it is up
}

void GuideScreen::runMenuItem(const int item) {
  menu.open = false;
  publish();
  gd::State s;
  s.screen = gd::Screen::List;
  switch (item) {
    case MENU_SEARCH:
      startSearch();
      return;
    case MENU_MARKS:
      s.list = gd::ListKind::Marks;
      break;
    case MENU_RECENT:
      s.list = gd::ListKind::Recent;
      break;
    case MENU_QUICK:
      s.list = gd::ListKind::Quick;
      break;
    case MENU_ABOUT:
      // About goes back where it came from: the list this screen belongs to, else the home.
      s = st;
      s.screen = gd::Screen::About;
      s.page = 0;
      s.sub = 0;
      break;
    default:
      return;
  }
  openScreen(s);
}

void GuideScreen::drawMenu(const MenuState& m) const {
  if (!m.open) return;
  gd::draw::MenuView view;
  view.title = tr(STR_GD_MENU_TITLE);
  view.labels[MENU_SEARCH] = tr(STR_GD_SEARCH);
  view.labels[MENU_MARKS] = tr(STR_GD_BOOKMARKS);
  view.labels[MENU_RECENT] = tr(STR_GD_RECENT);
  view.labels[MENU_QUICK] = tr(STR_GD_QUICK_CARDS);
  view.labels[MENU_ABOUT] = tr(STR_GD_ABOUT);
  view.count = MENU_COUNT;
  view.selected = m.selected;
  gd::draw::drawMenu(renderer, view);
}

void GuideScreen::startSearch() {
  saveState();
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_GD_SEARCH_TITLE), "",
                                                           gd::MAX_QUERY, InputType::Text);
  if (!keyboard) {
    LOG_ERR("GD", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    halfPending = true;
    if (result.isCancelled) return;
    const auto* typed = std::get_if<KeyboardResult>(&result.data);
    if (!typed) return;
    const std::string_view query = gd::trim(typed->text);
    if (query.empty()) return;
    gd::State s;
    s.screen = gd::Screen::List;
    s.list = gd::ListKind::Search;
    gd::copyCut(query, s.query, sizeof(s.query));
    openScreen(s);
  });
}

// ---- refresh -----------------------------------------------------------------------------------------

HalDisplay::RefreshMode GuideScreen::pickRefresh(const bool halfNow, const bool figure) {
  bool half = halfPending.exchange(false) || halfNow || rendersUntilHalf <= 1;
  if (!half && figure && ++figureScreens >= FIGURE_HALF_EVERY) half = true;
  if (half) {
    rendersUntilHalf = SETTINGS.getRefreshFrequency();
    figureScreens = 0;
    return HalDisplay::HALF_REFRESH;
  }
  rendersUntilHalf--;
  return HalDisplay::FAST_REFRESH;
}
