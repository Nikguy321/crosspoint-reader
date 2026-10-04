#include "SleepCardSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <memory>
#include <utility>

#include "CrossPointSettings.h"
#include "LocateMeActivity.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "sleepcards/CardText.h"
#include "sleepcards/CardTime.h"
#include "sleepcards/LocationFix.h"
#include "sleepcards/SleepCardSettings.h"
#include "util/LiveSleepPolicy.h"

namespace fui = freeink::ui;

namespace {
enum Row : uint8_t {
  LOCATION,
  LOCATE_ME,
  AUTO_LOCATE,
  WEATHER,
  WEATHER_UNITS,
  HUNTING,
  SEASON_START,
  SEASON_END,
  LEGAL_LIGHT,
  OWNER_NAME,
  OWNER_CONTACT_1,
  OWNER_CONTACT_2,
  QUOTE_SOURCE,
  DARK_CARDS,
  SHUFFLE_NOW_READING,
  SHUFFLE_DAY,
  SHUFFLE_CALENDAR,
  SHUFFLE_QUOTE,
  SHUFFLE_OWNER,
  SHUFFLE_SKY,
  SHUFFLE_WEATHER,  // before Pictures: buildScreen walks SHUFFLE_NOW_READING..SHUFFLE_PICTURES
  SHUFFLE_PICTURES,
  CARD_CYCLE_CHARGING,
  CHARGING_UPDATES,
};

const StrId menuNames[SleepCardSettingsActivity::MENU_ITEMS] = {
    StrId::STR_LOCATION,         StrId::STR_LOCATE_ME,           StrId::STR_AUTO_LOCATE,
    StrId::STR_WEATHER,          StrId::STR_WEATHER_UNITS,       StrId::STR_HUNTING_SEASON,
    StrId::STR_SEASON_START,     StrId::STR_SEASON_END,          StrId::STR_LEGAL_LIGHT,
    StrId::STR_OWNER_NAME,       StrId::STR_OWNER_CONTACT_1,     StrId::STR_OWNER_CONTACT_2,
    StrId::STR_QUOTE_SOURCE,     StrId::STR_DARK_CARDS,          StrId::STR_SHUFFLE_NOW_READING,
    StrId::STR_SHUFFLE_DAY,      StrId::STR_SHUFFLE_CALENDAR,    StrId::STR_SHUFFLE_QUOTE,
    StrId::STR_SHUFFLE_OWNER,    StrId::STR_SHUFFLE_SKY,         StrId::STR_SHUFFLE_WEATHER,
    StrId::STR_SHUFFLE_PICTURES, StrId::STR_CARD_CYCLE_CHARGING, StrId::STR_CHARGING_UPDATES};

uint8_t CrossPointSettings::* shuffleField(const int row) {
  switch (row) {
    case SHUFFLE_NOW_READING:
      return &CrossPointSettings::shuffleNowReading;
    case SHUFFLE_DAY:
      return &CrossPointSettings::shuffleDay;
    case SHUFFLE_CALENDAR:
      return &CrossPointSettings::shuffleCalendar;
    case SHUFFLE_QUOTE:
      return &CrossPointSettings::shuffleQuote;
    case SHUFFLE_OWNER:
      return &CrossPointSettings::shuffleOwner;
    case SHUFFLE_SKY:
      return &CrossPointSettings::shuffleSky;
    case SHUFFLE_WEATHER:
      return &CrossPointSettings::shuffleWeather;
    case SHUFFLE_PICTURES:
      return &CrossPointSettings::shufflePictures;
    default:
      return nullptr;
  }
}

// "Oct 1".
std::string monthDayLabel(const uint8_t month, const uint8_t day) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%s %u", sleepcards::monthShortName(month), static_cast<unsigned>(day));
  return buf;
}

// "10-01", the form the keyboard edits.
std::string monthDayEntry(const uint8_t month, const uint8_t day) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02u-%02u", static_cast<unsigned>(month), static_cast<unsigned>(day));
  return buf;
}

// The stored "47.6100,-122.3300" shown with a space after the comma.
std::string locationLabel(const char* stored) {
  double lat = 0;
  double lon = 0;
  if (!sleepcards::parseLocation(stored, lat, lon)) return tr(STR_NOT_SET);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.4f, %.4f", lat, lon);
  return buf;
}

void copyField(char* dest, const size_t cap, const std::string& text) { std::snprintf(dest, cap, "%s", text.c_str()); }

// A location entered on this screen: a new one is recorded as typed in, today (when the clock is
// set); one confirmed unchanged keeps the record of where it came from.
void recordTypedLocation(const char* before) {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  time_t now = 0;
  struct tm local{};
  if (halClock.utcEpoch(now) && sleepcards::plausibleTime(static_cast<int64_t>(now)) && halClock.localTime(local)) {
    year = static_cast<uint16_t>(local.tm_year + 1900);
    month = static_cast<uint8_t>(local.tm_mon + 1);
    day = static_cast<uint8_t>(local.tm_mday);
  }
  char record[sizeof(SETTINGS.sleepCardLocationFix)];
  if (!sleepcards::recordForTypedLocation(before, SETTINGS.sleepCardLocation, SETTINGS.sleepCardLocationFix, year,
                                          month, day, record, sizeof(record))) {
    record[0] = '\0';
  }
  std::snprintf(SETTINGS.sleepCardLocationFix, sizeof(SETTINGS.sleepCardLocationFix), "%s", record);
}
}  // namespace

SleepCardSettingsActivity::SleepCardSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("SleepCardSettings", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int SleepCardSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* SleepCardSettingsActivity::headerTitle() const { return tr(STR_SLEEP_CARDS); }

void SleepCardSettingsActivity::editText(const int row, const char* title, const char* initial,
                                         const size_t maxLength) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, title, initial, maxLength);
  if (!keyboard) {
    LOG_ERR("SCS", "OOM: KeyboardEntryActivity");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, row](const ActivityResult& result) {
    if (result.isCancelled) return;
    applyText(row, std::get<KeyboardResult>(result.data).text);
  });
}

void SleepCardSettingsActivity::applyText(const int row, const std::string& text) {
  bool ok = true;
  switch (row) {
    case LOCATION: {
      char stored[sizeof(SETTINGS.sleepCardLocation)];
      ok = sleepcards::normalizeLocation(text.c_str(), stored, sizeof(stored));
      if (ok) {
        char before[sizeof(SETTINGS.sleepCardLocation)];
        std::snprintf(before, sizeof(before), "%s", SETTINGS.sleepCardLocation);
        copyField(SETTINGS.sleepCardLocation, sizeof(SETTINGS.sleepCardLocation), stored);
        recordTypedLocation(before);
      }
      break;
    }
    case SEASON_START:
    case SEASON_END: {
      sleepcards::MonthDay md;
      ok = sleepcards::parseMonthDay(text.c_str(), md);
      if (ok && row == SEASON_START) {
        SETTINGS.huntStartMonth = md.month;
        SETTINGS.huntStartDay = md.day;
      } else if (ok) {
        SETTINGS.huntEndMonth = md.month;
        SETTINGS.huntEndDay = md.day;
      }
      break;
    }
    case OWNER_NAME:
      copyField(SETTINGS.ownerName, sizeof(SETTINGS.ownerName), text);
      break;
    case OWNER_CONTACT_1:
      copyField(SETTINGS.ownerContact1, sizeof(SETTINGS.ownerContact1), text);
      break;
    case OWNER_CONTACT_2:
      copyField(SETTINGS.ownerContact2, sizeof(SETTINGS.ownerContact2), text);
      break;
    default:
      return;
  }
  invalid_[row] = !ok;
  if (ok) {
    SETTINGS.saveToFile();
  } else {
    LOG_INF("SCS", "Row %d: entry refused, not saved", row);
  }
  requestUpdate();
}

void SleepCardSettingsActivity::activateIndex(const int index) {
  // Activation opens a keyboard or repaints a new value; a lingering flash would
  // gray an unrelated row.
  app.clearTapFlash();
  switch (index) {
    case LOCATION: {
      double lat = 0;
      double lon = 0;
      char initial[40] = "";
      if (sleepcards::parseLocation(SETTINGS.sleepCardLocation, lat, lon)) {
        std::snprintf(initial, sizeof(initial), "%.4f, %.4f", lat, lon);
      }
      editText(index, tr(STR_LOCATION_ENTRY), initial, 31);
      return;
    }
    case LOCATE_ME: {
      auto locate = makeUniqueNoThrow<LocateMeActivity>(renderer, mappedInput);
      if (!locate) {
        LOG_ERR("SCS", "OOM: LocateMeActivity");
        return;
      }
      // Returns here only when no radio ran (the exit otherwise reboots back to this screen).
      startActivityForResult(std::move(locate), [this](const ActivityResult&) {
        invalid_[LOCATION] = false;
        requestUpdate();
      });
      return;
    }
    case SEASON_START:
      editText(index, tr(STR_SEASON_DATE_ENTRY), monthDayEntry(SETTINGS.huntStartMonth, SETTINGS.huntStartDay).c_str(),
               8);
      return;
    case SEASON_END:
      editText(index, tr(STR_SEASON_DATE_ENTRY), monthDayEntry(SETTINGS.huntEndMonth, SETTINGS.huntEndDay).c_str(), 8);
      return;
    case OWNER_NAME:
      editText(index, tr(STR_OWNER_NAME), SETTINGS.ownerName, sizeof(SETTINGS.ownerName) - 1);
      return;
    case OWNER_CONTACT_1:
      editText(index, tr(STR_OWNER_CONTACT_1), SETTINGS.ownerContact1, sizeof(SETTINGS.ownerContact1) - 1);
      return;
    case OWNER_CONTACT_2:
      editText(index, tr(STR_OWNER_CONTACT_2), SETTINGS.ownerContact2, sizeof(SETTINGS.ownerContact2) - 1);
      return;
    case HUNTING:
      SETTINGS.huntingSeason =
          static_cast<uint8_t>((SETTINGS.huntingSeason + 1) % static_cast<uint8_t>(sleepcards::HuntMode::Count));
      break;
    case LEGAL_LIGHT:
      SETTINGS.legalLightRule =
          static_cast<uint8_t>((SETTINGS.legalLightRule + 1) % static_cast<uint8_t>(sleepcards::LegalLightRule::Count));
      break;
    case QUOTE_SOURCE:
      SETTINGS.quoteSources =
          static_cast<uint8_t>((SETTINGS.quoteSources + 1) % static_cast<uint8_t>(sleepcards::QuoteSource::Count));
      break;
    case AUTO_LOCATE:
      SETTINGS.autoLocateOnSync = SETTINGS.autoLocateOnSync ? 0 : 1;
      break;
    case WEATHER:
      SETTINGS.weatherEnabled = SETTINGS.weatherEnabled ? 0 : 1;
      break;
    case WEATHER_UNITS:
      SETTINGS.weatherUnits =
          static_cast<uint8_t>((SETTINGS.weatherUnits + 1) % static_cast<uint8_t>(sleepcards::WeatherUnits::Count));
      break;
    case DARK_CARDS:
      SETTINGS.darkCards = SETTINGS.darkCards ? 0 : 1;
      break;
    case CARD_CYCLE_CHARGING:
      SETTINGS.cardCycleWhenCharging = SETTINGS.cardCycleWhenCharging ? 0 : 1;
      break;
    case CHARGING_UPDATES:
      SETTINGS.chargingUpdateInterval =
          static_cast<uint8_t>((SETTINGS.chargingUpdateInterval + 1) % live_sleep::INTERVAL_COUNT);
      break;
    default: {
      const auto field = shuffleField(index);
      if (field == nullptr) return;
      SETTINGS.*field = SETTINGS.*field ? 0 : 1;
      break;
    }
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void SleepCardSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  static constexpr StrId HUNT_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON, StrId::STR_BETWEEN_DATES};
  static constexpr StrId LEGAL_LABELS[] = {StrId::STR_LEGAL_LIGHT_30_MIN, StrId::STR_LEGAL_LIGHT_CIVIL};
  // In sleepcards::WeatherUnits order.
  static constexpr StrId UNITS_LABELS[] = {StrId::STR_WEATHER_UNITS_METRIC, StrId::STR_WEATHER_UNITS_US};
  static_assert(std::size(UNITS_LABELS) == static_cast<size_t>(sleepcards::WeatherUnits::Count));
  // In sleepcards::QuoteSource order.
  static constexpr StrId QUOTE_LABELS[] = {StrId::STR_QUOTE_SOURCE_ALL,
                                           StrId::STR_QUOTE_SOURCE_BUILT_IN_MINE,
                                           StrId::STR_QUOTE_SOURCE_MINE_BOOKMARKS,
                                           StrId::STR_QUOTE_SOURCE_BUILT_IN_BOOKMARKS,
                                           StrId::STR_QUOTE_SOURCE_BUILT_IN,
                                           StrId::STR_QUOTE_SOURCE_MINE,
                                           StrId::STR_QUOTE_SOURCE_BOOKMARKS};
  static_assert(std::size(QUOTE_LABELS) == static_cast<size_t>(sleepcards::QuoteSource::Count));
  // In live_sleep::INTERVAL_MINUTES order.
  static constexpr StrId INTERVAL_LABELS[] = {StrId::STR_CHARGING_EVERY_1_MIN, StrId::STR_CHARGING_EVERY_2_MIN,
                                              StrId::STR_CHARGING_EVERY_5_MIN, StrId::STR_CHARGING_EVERY_10_MIN,
                                              StrId::STR_CHARGING_EVERY_15_MIN};
  static_assert(std::size(INTERVAL_LABELS) == live_sleep::INTERVAL_COUNT);
  const auto pick = [](const StrId* labels, const size_t count, const uint8_t value) {
    return I18N.get(labels[value < count ? value : 0]);
  };
  const auto textOrNotSet = [](const char* value) -> std::string { return value[0] ? value : tr(STR_NOT_SET); };

  rowValues_[LOCATION] = locationLabel(SETTINGS.sleepCardLocation);
  sleepcards::formatFixLine(sleepcards::describeLocation(SETTINGS.sleepCardLocationFix, SETTINGS.sleepCardLocation),
                            locationSource_, sizeof(locationSource_));
  rowItems_[LOCATION].subtitle = locationSource_[0] != '\0' ? locationSource_ : nullptr;
  rowValues_[LOCATE_ME].clear();
  rowValues_[AUTO_LOCATE] = SETTINGS.autoLocateOnSync ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  // What turning it on sends, and to whom.
  rowItems_[AUTO_LOCATE].subtitle = tr(STR_AUTO_LOCATE_HINT);
  rowValues_[WEATHER] = SETTINGS.weatherEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rowItems_[WEATHER].subtitle = tr(STR_WEATHER_HINT);  // what turning it on sends, and to whom
  rowValues_[WEATHER_UNITS] = pick(UNITS_LABELS, std::size(UNITS_LABELS), SETTINGS.weatherUnits);
  rowValues_[HUNTING] = pick(HUNT_LABELS, std::size(HUNT_LABELS), SETTINGS.huntingSeason);
  rowValues_[SEASON_START] = monthDayLabel(SETTINGS.huntStartMonth, SETTINGS.huntStartDay);
  rowValues_[SEASON_END] = monthDayLabel(SETTINGS.huntEndMonth, SETTINGS.huntEndDay);
  rowValues_[LEGAL_LIGHT] = pick(LEGAL_LABELS, std::size(LEGAL_LABELS), SETTINGS.legalLightRule);
  rowValues_[OWNER_NAME] = textOrNotSet(SETTINGS.ownerName);
  rowValues_[OWNER_CONTACT_1] = textOrNotSet(SETTINGS.ownerContact1);
  rowValues_[OWNER_CONTACT_2] = textOrNotSet(SETTINGS.ownerContact2);
  rowValues_[QUOTE_SOURCE] = pick(QUOTE_LABELS, std::size(QUOTE_LABELS), SETTINGS.quoteSources);
  for (int row = SHUFFLE_NOW_READING; row <= SHUFFLE_PICTURES; row++) {
    rowValues_[row] = SETTINGS.*shuffleField(row) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }
  rowValues_[DARK_CARDS] = SETTINGS.darkCards ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rowItems_[DARK_CARDS].subtitle = tr(STR_DARK_CARDS_HINT);
  rowValues_[CARD_CYCLE_CHARGING] = SETTINGS.cardCycleWhenCharging ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rowItems_[CARD_CYCLE_CHARGING].subtitle = tr(STR_CARD_CYCLE_CHARGING_HINT);
  // An index from a newer firmware reads as the default, as live_sleep::intervalMinutes() does.
  rowValues_[CHARGING_UPDATES] =
      pick(INTERVAL_LABELS, std::size(INTERVAL_LABELS),
           SETTINGS.chargingUpdateInterval < live_sleep::INTERVAL_COUNT ? SETTINGS.chargingUpdateInterval
                                                                        : live_sleep::DEFAULT_INTERVAL_INDEX);
  rowItems_[CHARGING_UPDATES].subtitle = tr(STR_CHARGING_UPDATES_HINT);
  for (int i = 0; i < MENU_ITEMS; i++) {
    if (invalid_[i]) rowValues_[i] = tr(STR_INVALID_ENTRY);
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  // The hints wrap (the Weather one names both services that receive the location).
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 3;
  syncListViewport(screen, props);
  screen.list(props);
}
