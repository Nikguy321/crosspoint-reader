#include "TapZoneSettingsActivity.h"

#if FREEINK_CAP_TOUCH
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

TapZoneSettingsActivity::TapZoneSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TapZoneSettings", renderer, mappedInput) {}

void TapZoneSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  rows[0].label = tr(STR_TAP_ZONE_CURRENT);
  rows[1].label = tr(STR_TAP_ZONE_STEPPED);
  rows[1].subtitle = tr(STR_TAP_ZONE_HINT);
  rows[0].actionValue = 0;
  rows[1].actionValue = 1;
  nav.selected = SETTINGS.tapZoneMap;
}

const char* TapZoneSettingsActivity::headerTitle() const { return tr(STR_TAP_ZONE_LAYOUT); }

void TapZoneSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= CrossPointSettings::TAP_ZONE_MAP_COUNT || SETTINGS.tapZoneMap == index) return;
  SETTINGS.tapZoneMap = static_cast<uint8_t>(index);
  SETTINGS.saveToFile();
  requestUpdate();
}

void TapZoneSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int previewTop =
      std::max(height / 3, metrics.topPadding + metrics.headerHeight +
                               2 * (screen.theme().listTouchMinRowHeight + screen.theme().listTouchRowGap) + 24);
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(height - previewTop), 0});

  for (int i = 0; i < 2; ++i) rows[i].value = SETTINGS.tapZoneMap == i ? tr(STR_SELECTED) : nullptr;
  fui::ListProps props;
  props.items = rows;
  props.count = 2;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);

  const int previewHeight = std::min(height - previewTop - 20, (width - 32) * 4 / 3);
  const int previewWidth = previewHeight * 3 / 4;
  const int x = (width - previewWidth) / 2;
  const int y = previewTop;
  const int left = x + previewWidth / 3;
  const int right = x + previewWidth - previewWidth / 3;
  const int top = y + previewHeight / 3;
  const int bottom = y + previewHeight - previewHeight / 3;
  auto& target = screen.frame().target();
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.stroke(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(previewWidth),
                          static_cast<int16_t>(previewHeight)},
                ink, 1);
  const auto line = [&](int x1, int y1, int x2, int y2) {
    target.line(fui::Point{static_cast<int16_t>(x1), static_cast<int16_t>(y1)},
                fui::Point{static_cast<int16_t>(x2), static_cast<int16_t>(y2)}, 1, ink);
  };
  if (SETTINGS.tapZoneMap == CrossPointSettings::TAP_ZONE_STEPPED) {
    line(right, y, right, bottom);
    line(left, top, right, top);
    line(left, top, left, y + previewHeight);
    line(left, bottom, right, bottom);
  } else {
    line(left, y, left, y + previewHeight);
    line(left, top, right, top);
    line(right, top, right, bottom);
    line(left, bottom, right, bottom);
  }

  auto labelStyle = screen.theme().smallText;
  labelStyle.align = fui::TextAlign::Center;
  const auto label = [&](int cx, int cy, const char* text) {
    target.text(fui::Rect{static_cast<int16_t>(cx - previewWidth / 6), static_cast<int16_t>(cy - 10),
                          static_cast<int16_t>(previewWidth / 3), 20},
                text, labelStyle);
  };
  label(SETTINGS.tapZoneMap == CrossPointSettings::TAP_ZONE_STEPPED ? x + previewWidth / 3 : x + previewWidth / 6,
        SETTINGS.tapZoneMap == CrossPointSettings::TAP_ZONE_STEPPED ? y + previewHeight / 6 : y + previewHeight / 2,
        tr(STR_TAP_ZONE_PREV));
  label(x + previewWidth * 5 / 6, y + previewHeight / 2, tr(STR_TAP_ZONE_NEXT));
  label(x + previewWidth / 2, y + previewHeight / 2, tr(STR_TAP_ZONE_MENU));
}
#endif
