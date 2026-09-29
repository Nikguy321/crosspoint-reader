#include "LocateMeActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/GeolocateClient.h"
#include "network/RadioPower.h"
#include "sleepcards/CardTime.h"
#include "sleepcards/LocationFix.h"
#include "sleepcards/SleepCardSettings.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;

constexpr const char* BEACONDB_URL = "https://api.beacondb.net/v1/geolocate";
// Only the fields read, which keeps the answer to ~150 bytes.
constexpr const char* IPWHOIS_URL = "https://ipwho.is/?fields=success,message,latitude,longitude,city,region";

// Scan results kept for the request (the strongest 20 are sent); a crowded scan is cut here.
constexpr size_t MAX_SCAN = 48;

// "47.62, -122.35": two decimals (~1 km) is all a place line needs.
void shortCoordinates(const double lat, const double lon, char* out, const size_t cap) {
  std::snprintf(out, cap, "%.2f, %.2f", lat, lon);
}
}  // namespace

LocateMeActivity::LocateMeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("LocateMe", renderer, mappedInput), UiAppHost(renderer) {}

void LocateMeActivity::onEnter() {
  Activity::onEnter();
  state = State::Intro;
  selected = 0;
  fix = geolocate::Fix{};
  resetUi();
  app.on(ACTION_ROW, &LocateMeActivity::onRow, this);
  app.setScreen(&LocateMeActivity::screenFn, this);
  requestUpdate();
}

void LocateMeActivity::onExit() {
  Activity::onExit();
  // Every network activity leaves by rebooting: it frees the heap the Wi-Fi driver fragmented and
  // takes the radio (and its clock lock) down. Settings are already saved by then; the reboot
  // lands back on Sleep Screen Cards, where the Location row shows what was saved.
  if (radioStarted && (WiFi.getMode() != WIFI_MODE_NULL || RadioPower::ranThisBoot())) {
    if (WiFi.getMode() != WIFI_MODE_NULL) WiFi.disconnect(false);
    delay(30);
    silentRestartToSleepCards();
  }
}

void LocateMeActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<LocateMeActivity*>(user)->buildScreen(screen);
}

void LocateMeActivity::onRow(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<LocateMeActivity*>(user);
  self->app.clearTapFlash();
  self->activate(event.value);
}

int LocateMeActivity::actionCount() const {
  if (state == State::Failed) return failure == Failure::Unreachable ? 2 : 1;
  return 2;
}

void LocateMeActivity::activate(const int row) {
  if (state == State::Intro) {
    if (row == 0) {
      startLocate();
    } else {
      finish();
    }
  } else if (state == State::Result) {
    if (row == 0) {
      save();
    } else {
      LOG_INF("LOC", "Location not saved");
      finish();
    }
  } else if (state == State::Failed) {
    if (row == 0 && failure == Failure::Unreachable) {
      chooseNetwork();
    } else {
      finish();
    }
  }
}

void LocateMeActivity::startLocate() {
  LOG_INF("LOC", "Locate requested");
  selected = 0;
  if (WiFi.status() == WL_CONNECTED) {
    showStatus(StrId::STR_LOCATE_SCANNING);
    return;
  }
  joinNetwork(true);
}

// The joined network may not reach the internet (a sync peer's or a hub's own hotspot is often
// the last network used): the list lets another one be picked.
void LocateMeActivity::chooseNetwork() {
  LOG_INF("LOC", "Choosing another network");
  selected = 0;
  pickingNetwork = true;
  joinNetwork(false);
}

void LocateMeActivity::joinNetwork(const bool autoConnect) {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, autoConnect);
  if (!wifi) {
    LOG_ERR("LOC", "OOM: WifiSelectionActivity");
    pickingNetwork = false;
    fail(Failure::NoMemory);
    return;
  }
  radioStarted = true;
  state = State::Joining;
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) { onWifiDone(!result.isCancelled); });
}

void LocateMeActivity::onWifiDone(const bool connected) {
  const bool picking = pickingNetwork;
  pickingNetwork = false;
  if (!connected || WiFi.status() != WL_CONNECTED) {
    LOG_INF("LOC", "No Wi-Fi joined");
    // The join (or its network list) started the radio; nothing more will use it.
    radioOff();
    selected = 0;
    // Backing out of a network list opened from the failure screen returns to that screen.
    fail(picking ? failure : Failure::NoWifi);
    return;
  }
  showStatus(StrId::STR_LOCATE_SCANNING);
}

// A failure screen waits for a tap with the radio fully off (and the clock free to drop), and a
// network picked from it afterwards starts the radio afresh. Only a radio this activity started.
void LocateMeActivity::radioOff() {
  if (radioStarted) RadioPower::off();
}

void LocateMeActivity::showStatus(const StrId next) {
  status = next;
  state = State::Working;
  requestUpdate();
}

void LocateMeActivity::fail(const Failure why) {
  failure = why;
  selected = 0;
  state = State::Failed;
  requestUpdate();
}

namespace {
geolocate::Reply replyOf(const GeolocateClient::Result result) {
  switch (result) {
    case GeolocateClient::Result::Ok:
    case GeolocateClient::Result::TooLarge:
      return geolocate::Reply::Answered;
    case GeolocateClient::Result::NoMemory:
      return geolocate::Reply::NoMemory;
    case GeolocateClient::Result::Transport:
      break;
  }
  return geolocate::Reply::NoAnswer;
}
}  // namespace

void LocateMeActivity::runLookup() {
  // Heap, freed on return: the scan copy (48 x 40 B), the request body (1.3 KB) and one response
  // (4 KB). Too big for the loop task's stack, and only alive for this one lookup.
  auto aps = makeUniqueNoThrow<geolocate::AccessPoint[]>(MAX_SCAN);
  auto request = makeUniqueNoThrow<char[]>(geolocate::REQUEST_CAP);
  auto response = makeUniqueNoThrow<char[]>(geolocate::RESPONSE_CAP + 1);
  if (!aps || !request || !response) {
    LOG_ERR("LOC", "OOM: lookup buffers");
    radioOff();
    fail(Failure::NoMemory);
    return;
  }

  // The scan: a blocking station scan on the joined radio.
  int16_t found = 0;
  const size_t apCount = GeolocateClient::scanAccessPoints(aps.get(), MAX_SCAN, found);
  const size_t sent = geolocate::buildRequestBody(aps.get(), apCount, request.get(), geolocate::REQUEST_CAP);
  aps.reset();
  LOG_INF("LOC", "Scan: %d networks, %u usable for Wi-Fi lookup", static_cast<int>(found), static_cast<unsigned>(sent));

  geolocate::Fix wifiFix;
  geolocate::Fix ipFix;
  geolocate::Reply beaconReply = geolocate::Reply::NotAsked;
  geolocate::Reply ipReply = geolocate::Reply::NotAsked;
  size_t length = 0;
  int httpStatus = 0;
  if (sent >= geolocate::MIN_REQUEST_APS) {
    status = StrId::STR_LOCATE_ASKING_WIFI;
    requestUpdateAndWait();
    const auto result = GeolocateClient::request(BEACONDB_URL, request.get(), response.get(),
                                                 geolocate::RESPONSE_CAP + 1, length, httpStatus);
    beaconReply = replyOf(result);
    if (result == GeolocateClient::Result::Ok && httpStatus == 200) {
      if (!geolocate::parseBeaconDbResponse(response.get(), length, wifiFix))
        LOG_INF("LOC", "beaconDB: no usable answer");
    } else {
      LOG_INF("LOC", "beaconDB: result %d, HTTP %d", static_cast<int>(result), httpStatus);
    }
  }

  if (geolocate::needsIpLookup(wifiFix)) {
    status = StrId::STR_LOCATE_ASKING_IP;
    requestUpdateAndWait();
    const auto result =
        GeolocateClient::request(IPWHOIS_URL, nullptr, response.get(), geolocate::RESPONSE_CAP + 1, length, httpStatus);
    ipReply = replyOf(result);
    if (result == GeolocateClient::Result::Ok && httpStatus == 200) {
      if (!geolocate::parseIpWhoisResponse(response.get(), length, ipFix)) LOG_INF("LOC", "ipwho.is: no usable answer");
    } else {
      LOG_INF("LOC", "ipwho.is: result %d, HTTP %d", static_cast<int>(result), httpStatus);
    }
  }

  const geolocate::Fix* chosen = geolocate::chooseFix(wifiFix, ipFix);
  if (chosen == nullptr) {
    radioOff();
    switch (geolocate::classifyFailure(sent, beaconReply, ipReply)) {
      case geolocate::LookupFailure::TooFew:
        fail(Failure::TooFew);
        break;
      case geolocate::LookupFailure::NotFound:
        fail(Failure::NotFound);
        break;
      case geolocate::LookupFailure::NoMemory:
        fail(Failure::NoMemory);
        break;
      case geolocate::LookupFailure::Unreachable:
        fail(Failure::Unreachable);
        break;
    }
    return;
  }
  // The result screen needs no radio; a session this activity started ends with the reboot.
  if (radioStarted) RadioPower::stop();
  fix = *chosen;
  LOG_INF("LOC", "located: %s, accuracy %lu m", fix.source == geolocate::FixSource::Wifi ? "wifi" : "ip",
          static_cast<unsigned long>(fix.accuracyM));
  selected = 0;
  state = State::Result;
  requestUpdate();
}

void LocateMeActivity::save() {
  sleepcards::LocationFix record;
  if (!sleepcards::formatLocation(fix.lat, fix.lon, record.location, sizeof(record.location))) {
    LOG_ERR("LOC", "Location did not format");
    fail(Failure::SaveFailed);
    return;
  }
  // The same check a typed entry passes, so every card reads it the same way.
  char stored[sizeof(SETTINGS.sleepCardLocation)];
  if (!sleepcards::normalizeLocation(record.location, stored, sizeof(stored)) || stored[0] == '\0') {
    LOG_ERR("LOC", "Location refused");
    fail(Failure::SaveFailed);
    return;
  }
  record.source = fix.source == geolocate::FixSource::Wifi ? sleepcards::LocationSource::Wifi
                                                           : sleepcards::LocationSource::Internet;
  record.accuracyM = fix.accuracyM > sleepcards::MAX_FIX_ACCURACY_M ? sleepcards::MAX_FIX_ACCURACY_M : fix.accuracyM;
  time_t now = 0;
  struct tm local{};
  if (halClock.utcEpoch(now) && sleepcards::plausibleTime(static_cast<int64_t>(now)) && halClock.localTime(local)) {
    record.year = static_cast<uint16_t>(local.tm_year + 1900);
    record.month = static_cast<uint8_t>(local.tm_mon + 1);
    record.day = static_cast<uint8_t>(local.tm_mday);
  }
  std::snprintf(SETTINGS.sleepCardLocation, sizeof(SETTINGS.sleepCardLocation), "%s", stored);
  if (!sleepcards::formatLocationFix(record, SETTINGS.sleepCardLocationFix, sizeof(SETTINGS.sleepCardLocationFix))) {
    SETTINGS.sleepCardLocationFix[0] = '\0';
  }
  SETTINGS.saveToFile();
  LOG_INF("LOC", "Location saved");
  finish();
}

void LocateMeActivity::loop() {
  switch (state) {
    case State::Joining:
      return;  // WifiSelectionActivity is on top
    case State::Working:
      // First pass: paint the status, then block on the lookup (it repaints each step).
      requestUpdateAndWait();
      runLookup();
      return;
    case State::Intro:
    case State::Result:
    case State::Failed:
      break;
  }

  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;  // dispatched to onRow

  if (actionCount() > 1 && (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right))) {
    selected = 1 - selected;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selected < actionCount() ? selected : 0);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == State::Result) LOG_INF("LOC", "Location not saved");
    finish();
  }
}

void LocateMeActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Text lines share the action rows' content edges.
  const auto indent = static_cast<int16_t>(screen.theme().listInset + screen.theme().listSidePadding);
  const auto textBlock = [&](const char* text, fui::TextStyle style, const uint8_t lines, const int16_t gap) {
    style.maxLines = lines;
    const auto height = static_cast<int16_t>(screen.target().lineHeight(style.font) * lines);
    fui::Rect r = screen.takeTop(height, gap);
    r.x = static_cast<int16_t>(r.x + indent);
    r.width = static_cast<int16_t>(r.width - indent * 2);
    screen.target().text(r, text, style);
  };

  // Themes may set the body style bold; the paragraphs here are regular, only the headline is bold.
  auto body = screen.theme().bodyText;
  body.align = fui::TextAlign::Center;
  body.bold = false;
  auto bold = body;
  bold.bold = true;
  auto small = screen.theme().smallText;
  small.align = fui::TextAlign::Center;
  small.bold = false;

  fui::ListItem actions[2];
  if (state == State::Intro) {
    textBlock(tr(STR_LOCATE_INTRO), body, 10, screen.theme().spaceLg);
    actions[0].label = tr(STR_LOCATE);
    actions[1].label = tr(STR_CANCEL);
  } else if (state == State::Result) {
    char place[96];
    const bool named = fix.place[0] != '\0';
    if (named) {
      std::snprintf(place, sizeof(place), "%s", fix.place);
    } else {
      char coords[40];
      shortCoordinates(fix.lat, fix.lon, coords, sizeof(coords));
      std::snprintf(place, sizeof(place), tr(STR_LOCATE_NEAR), coords);
    }
    char accuracy[96];
    const bool fromWifi = fix.source == geolocate::FixSource::Wifi;
    if (fromWifi) {
      char amount[16];
      sleepcards::formatAccuracy(fix.accuracyM, amount, sizeof(amount));
      std::snprintf(accuracy, sizeof(accuracy), tr(STR_LOCATE_ABOUT_WIFI), amount);
    } else {
      std::snprintf(accuracy, sizeof(accuracy), "%s", tr(STR_LOCATE_CITY_LEVEL));
    }

    textBlock(place, bold, 2, screen.theme().spaceSm);
    textBlock(accuracy, body, 2, screen.theme().spaceSm);
    // An address lookup names the carrier's or the VPN's city as readily as the reader's own.
    if (!fromWifi) textBlock(tr(STR_LOCATE_IP_CAUTION), small, 2, screen.theme().spaceSm);
    // "Near 47.62, -122.35" already says where; a named place gets its coordinates too.
    if (named) {
      char exact[40];
      std::snprintf(exact, sizeof(exact), "%.4f, %.4f", fix.lat, fix.lon);
      textBlock(exact, small, 1, screen.theme().spaceSm);
    }
    screen.spacer(screen.theme().spaceLg);
    actions[0].label = tr(STR_LOCATE_SAVE);
    actions[1].label = tr(STR_CANCEL);
  } else if (state == State::Failed) {
    StrId title = StrId::STR_LOCATE_UNREACHABLE;
    StrId hint = StrId::STR_LOCATE_UNREACHABLE_HINT;
    switch (failure) {
      case Failure::NoWifi:
        title = StrId::STR_LOCATE_NO_WIFI;
        hint = StrId::STR_LOCATE_NO_WIFI_HINT;
        break;
      case Failure::TooFew:
        title = StrId::STR_LOCATE_TOO_FEW;
        hint = StrId::STR_LOCATE_TOO_FEW_HINT;
        break;
      case Failure::NotFound:
        title = StrId::STR_LOCATE_NOT_FOUND;
        hint = StrId::STR_LOCATE_NOT_FOUND_HINT;
        break;
      case Failure::NoMemory:
        title = StrId::STR_LOCATE_NO_MEMORY;
        hint = StrId::STR_LOCATE_NO_MEMORY_HINT;
        break;
      case Failure::SaveFailed:
        title = StrId::STR_LOCATE_SAVE_FAILED;
        hint = StrId::STR_LOCATE_SAVE_FAILED_HINT;
        break;
      case Failure::Unreachable:
        break;
    }
    textBlock(I18N.get(title), bold, 2, screen.theme().spaceSm);
    textBlock(I18N.get(hint), body, 4, screen.theme().spaceLg);
    if (failure == Failure::Unreachable) {
      actions[0].label = tr(STR_LOCATE_CHOOSE_NETWORK);
      actions[1].label = tr(STR_DONE);
    } else {
      actions[0].label = tr(STR_DONE);
    }
  } else {
    return;
  }

  const int count = actionCount();
  actions[0].actionValue = 0;
  actions[1].actionValue = 1;
  fui::ListProps props;
  props.items = actions;
  props.count = static_cast<uint16_t>(count);
  props.selectedIndex = static_cast<int16_t>(selected < count ? selected : 0);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.scrollIndicator = false;
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    rowHeight = static_cast<int16_t>(metrics.listRowHeight);
    props.rowHeight = rowHeight;
  }
  screen.list(props, static_cast<int16_t>(rowHeight * count + screen.theme().listRowGap * (count - 1) +
                                          screen.theme().spaceSm));
}

void LocateMeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 state == State::Result ? tr(STR_LOCATE_FOUND) : tr(STR_LOCATE_ME));

  const int midY = pageHeight / 2;
  switch (state) {
    case State::Intro:
    case State::Result:
    case State::Failed: {
      renderUi();
      const bool twoRows = actionCount() > 1;
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), twoRows ? tr(STR_DIR_UP) : "",
                                                twoRows ? tr(STR_DIR_DOWN) : "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case State::Joining:
    case State::Working: {
      renderer.drawCenteredText(UI_12_FONT_ID, midY, I18N.get(status));
      // The lookup blocks the buttons until it is done (see GeolocateClient's timeouts).
      if (state == State::Working) {
        renderer.drawCenteredText(UI_10_FONT_ID, midY + renderer.getLineHeight(UI_12_FONT_ID) * 2,
                                  tr(STR_LOCATE_WAIT_HINT));
      }
      break;
    }
  }
  renderer.displayBuffer();
}
