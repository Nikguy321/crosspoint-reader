#include "AiAskActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "AiActivity.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/RadioPower.h"

namespace {

using Button = MappedInputManager::Button;

// The seconds on screen move in these steps (each a FAST refresh while Gemini thinks).
constexpr uint32_t SECONDS_STEP = 10;
constexpr int MARGIN = 24;
constexpr int CANCEL_W = 220;
constexpr int CANCEL_H = 64;

}  // namespace

AiAskActivity::AiAskActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string question)
    : Activity("AiAsk", renderer, mappedInput), question(std::move(question)) {}

void AiAskActivity::onEnter() {
  Activity::onEnter();
  startMs = millis();
  publish(nullptr);
  if (WiFi.status() == WL_CONNECTED) {
    // A station already up (started by someone else): used, so leaving the app still reboots to free
    // the heap the TLS requests fragmented - but left on after the question, as it was found.
    AiActivity::noteRadioUsed();
    state = State::Working;
    requestUpdate();
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/true);
  if (!wifi) {
    LOG_ERR("AI", "OOM: WifiSelectionActivity");
    finishWith(AiRun::addFailed(question.c_str(), tr(STR_MEMORY_ERROR)));
    return;
  }
  // The join starts the radio (through RadioPower); leaving the app reboots to free its heap.
  AiActivity::noteRadioUsed();
  radioStarted = true;
  state = State::Joining;
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
      LOG_INF("AI", "No Wi-Fi joined: the question was not sent");
      RadioPower::off();
      finishWith(AiRun::addFailed(question.c_str(), tr(STR_AI_NO_WIFI)));
      return;
    }
    state = State::Working;
    startMs = millis();
    publish(nullptr);
  });
}

void AiAskActivity::finishWith(const int exchange) {
  state = State::Done;
  MenuResult result;
  result.action = exchange;
  setResult(std::move(result));
  finish();
}

void AiAskActivity::publish(const AiRun::Progress* progress) {
  if (progress != nullptr) last = *progress;
  Frame f;
  std::snprintf(f.stage, sizeof(f.stage), "%s", last.stage != nullptr ? last.stage : "");
  std::snprintf(f.model, sizeof(f.model), "%s", last.model);
  const uint32_t now = millis();
  f.seconds = (now - startMs) / 1000;
  f.waitSeconds = last.waitUntilMs != 0 && static_cast<int32_t>(last.waitUntilMs - now) > 0
                      ? (last.waitUntilMs - now + 999) / 1000
                      : 0;
  f.cancelling = cancelRequested;
  taskENTER_CRITICAL(&frameLock);
  shared = f;
  taskEXIT_CRITICAL(&frameLock);
}

bool AiAskActivity::cancelTapped(const int x, const int y) const {
  const int left = (renderer.getScreenWidth() - CANCEL_W) / 2;
  return x >= left && x < left + CANCEL_W && y >= cancelTop.load() && y < cancelBottom.load();
}

// AiRun's tick, between the waits' slices on this (blocked) loop task: the input is pumped here, as an
// OPDS download does - Back, the left key, a tap on Cancel or Home cancel; Home's own action is kept
// for the main loop's next pass, which then leaves the app.
bool AiAskActivity::onTick(void* ctx) {
  auto* self = static_cast<AiAskActivity*>(ctx);
  MappedInputManager& in = self->mappedInput;
  in.update(true);
  int x = 0;
  int y = 0;
  const bool cancel = in.wasReleased(Button::Back) || in.wasReleased(Button::Up) || in.wasHomeGesture() ||
                      (in.wasScreenTapped(x, y) && self->cancelTapped(x, y));
  if (cancel && !self->cancelRequested) {
    LOG_INF("AI", "Cancel");
    self->cancelRequested = true;
    self->publish(nullptr);
    self->requestUpdate(true);
  }
  const uint32_t seconds = (millis() - self->startMs) / 1000;
  if (seconds / SECONDS_STEP != self->lastSeconds / SECONDS_STEP) {
    self->lastSeconds = seconds;
    self->publish(nullptr);
    self->requestUpdate(true);
  }
  return !self->cancelRequested;
}

void AiAskActivity::onChanged(const AiRun::Progress& progress, void* ctx) {
  auto* self = static_cast<AiAskActivity*>(ctx);
  self->publish(&progress);
  self->requestUpdate(true);
}

void AiAskActivity::runQuestion() {
  // The screen comes with AiRun's first stage ("Looking up Wikipedia...", "Asking Gemini...").
  AiRun::Hooks hooks;
  hooks.tick = &AiAskActivity::onTick;
  hooks.changed = &AiAskActivity::onChanged;
  hooks.ctx = this;
  AiRun::Outcome out;
  AiRun::ask(question.c_str(), hooks, out);
  // Wi-Fi is for the question only - when this screen turned it on.
  if (radioStarted) RadioPower::off();
  finishWith(out.cancelled ? -1 : out.exchange);
}

void AiAskActivity::loop() {
  if (state == State::Working) runQuestion();
}

void AiAskActivity::render(RenderLock&&) {
  if (state == State::Joining) return;  // WifiSelectionActivity is on top
  Frame f;
  taskENTER_CRITICAL(&frameLock);
  f = shared;
  taskEXIT_CRITICAL(&frameLock);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int textWidth = width - 2 * MARGIN;
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_AI));

  int y = metrics.topPadding + metrics.headerHeight + 30;
  const int line12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int line10 = renderer.getLineHeight(UI_10_FONT_ID);
  for (const auto& line : renderer.wrappedText(UI_12_FONT_ID, question.c_str(), textWidth, 7, EpdFontFamily::BOLD)) {
    renderer.drawText(UI_12_FONT_ID, MARGIN, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += line12;
  }
  y += line12;
  const char* stage = f.cancelling ? tr(STR_AI_CANCELLING) : f.stage;
  for (const auto& line : renderer.wrappedText(UI_12_FONT_ID, stage, textWidth, 4)) {
    renderer.drawText(UI_12_FONT_ID, MARGIN, y, line.c_str());
    y += line12;
  }
  y += line10 / 2;
  char status[GEM_LABEL_MAX + 32];
  if (f.waitSeconds > 0) {
    std::snprintf(status, sizeof(status), tr(STR_AI_RETRY_WAIT), static_cast<unsigned long>(f.waitSeconds));
  } else if (f.model[0]) {
    std::snprintf(status, sizeof(status), tr(STR_AI_ELAPSED), f.model, static_cast<unsigned long>(f.seconds));
  } else {
    std::snprintf(status, sizeof(status), tr(STR_AI_ELAPSED_NO_MODEL), static_cast<unsigned long>(f.seconds));
  }
  renderer.drawText(UI_10_FONT_ID, MARGIN, y, status);

  // Cancel, a framed button above the bottom band, and how else to cancel under it.
  const int top = height - metrics.buttonHintsHeight - CANCEL_H - 3 * line10;
  const int left = (width - CANCEL_W) / 2;
  if (!f.cancelling) {
    renderer.drawRect(left, top, CANCEL_W, CANCEL_H, 2, true);
    const int labelY = top + (CANCEL_H - line12) / 2;
    const int labelW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_AI_CANCEL), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, left + (CANCEL_W - labelW) / 2, labelY, tr(STR_AI_CANCEL), true,
                      EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + CANCEL_H + line10 / 2, tr(STR_AI_CANCEL_HINT));
  }
  cancelTop.store(top);
  cancelBottom.store(top + CANCEL_H);
  renderer.displayBuffer(firstFrame ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstFrame = false;
}
