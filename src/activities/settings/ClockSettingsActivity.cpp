#include "ClockSettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>

#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "TimezonePickerActivity.h"
#include "components/UITheme.h"
#include "util/Timezones.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_TIMEZONE = 0,
  ITEM_TZ_FROM_LOCATION,
  ITEM_DST,
  ITEM_FORMAT,
  ITEM_SHOW_ON_HOME,
  ITEM_SYNC,
};

const StrId menuNames[ClockSettingsActivity::ITEM_COUNT] = {
    StrId::STR_TIMEZONE,     StrId::STR_TZ_FROM_LOCATION, StrId::STR_CLOCK_DST,
    StrId::STR_CLOCK_FORMAT, StrId::STR_CLOCK_IN_HEADER,  StrId::STR_CLOCK_SYNC_NOW,
};

const StrId dstNames[CrossPointSettings::CLOCK_DST_MODE_COUNT] = {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON,
                                                                  StrId::STR_STATE_OFF};
}  // namespace

ClockSettingsActivity::ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ClockSettings", renderer, mappedInput) {}

void ClockSettingsActivity::onEnter() {
  // The rows are in place before the base requests the first frame.
  rowCount_ = 0;
  for (int item = 0; item < ITEM_COUNT; item++) {
    // The locations it follows come from Locate Me, which only the X4 Pro has.
    if (item == ITEM_TZ_FROM_LOCATION && !BoardConfig::isX4Pro()) continue;
    rows_[rowCount_] = item;
    rowItems_[rowCount_].label = I18N.get(menuNames[item]);
    rowItems_[rowCount_].actionValue = static_cast<int16_t>(rowCount_);
    rowCount_++;
  }
  UiListActivity::onEnter();
}

const char* ClockSettingsActivity::headerTitle() const { return tr(STR_CLOCK); }

void ClockSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount_) return;
  nav.selected = index;
  app.clearTapFlash();
  switch (rows_[index]) {
    case ITEM_TIMEZONE:
      if (auto activity = makeUniqueNoThrow<TimezonePickerActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: TimezonePickerActivity");
      }
      return;
    case ITEM_TZ_FROM_LOCATION:
      SETTINGS.clockZoneFromLocation = SETTINGS.clockZoneFromLocation ? 0 : 1;
      break;
    case ITEM_DST:
      SETTINGS.clockDst = (SETTINGS.clockDst + 1) % CrossPointSettings::CLOCK_DST_MODE_COUNT;
      timezones::applyToClock();
      break;
    case ITEM_FORMAT:
      SETTINGS.clockFormat = (SETTINGS.clockFormat + 1) % 2;
      break;
    case ITEM_SHOW_ON_HOME:
      SETTINGS.clockShowInHeader = (SETTINGS.clockShowInHeader + 1) % 2;
      break;
    case ITEM_SYNC:
      if (auto activity = makeUniqueNoThrow<ClockSyncActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: ClockSyncActivity");
      }
      return;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void ClockSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Every value is a flash/translation string or the member time buffer, so
  // the render pass allocates nothing.
  const uint8_t dst = SETTINGS.clockDst < CrossPointSettings::CLOCK_DST_MODE_COUNT ? SETTINGS.clockDst : uint8_t{0};
  for (int row = 0; row < rowCount_; row++) {
    fui::ListItem& item = rowItems_[row];
    switch (rows_[row]) {
      case ITEM_TIMEZONE:
        item.value = timezones::table()[timezones::activeIndex()].name;
        break;
      case ITEM_TZ_FROM_LOCATION:
        item.value = SETTINGS.clockZoneFromLocation ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        // What sets it, and the time zone map's ODbL credit.
        item.subtitle = tr(STR_TZ_FROM_LOCATION_HINT);
        break;
      case ITEM_DST:
        item.value = I18N.get(dstNames[dst]);
        break;
      case ITEM_FORMAT:
        item.value = SETTINGS.clockFormat == 1 ? tr(STR_CLOCK_FORMAT_12H) : tr(STR_CLOCK_FORMAT_24H);
        break;
      case ITEM_SHOW_ON_HOME:
        item.value = SETTINGS.clockShowInHeader ? tr(STR_SHOW) : tr(STR_HIDE);
        break;
      case ITEM_SYNC:
        // The sync row's value is the current time itself: it confirms the sync,
        // previews format/zone changes, and reads "Not Set" until the first sync.
        item.value =
            SETTINGS.clockHasBeenSynced && halClock.formatTime(syncTime_, sizeof(syncTime_), SETTINGS.clockFormat == 1)
                ? syncTime_
                : tr(STR_NOT_SET);
        break;
      default:
        break;
    }
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(rowCount_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 3;
  syncListViewport(screen, props);
  screen.list(props);
}
