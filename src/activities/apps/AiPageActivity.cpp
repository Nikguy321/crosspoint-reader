#include "AiPageActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <gemini.h>

#include <cstdio>
#include <cstring>

#include "AiText.h"
#include "CrossPointSettings.h"
#include "GuideDraw.h"
#include "GuideMetrics.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "network/AiRun.h"

namespace {

using Button = MappedInputManager::Button;

constexpr unsigned long KEY_LONG_PRESS_MS = 600;

std::string buildText(const char* question, const char* note, const char* body) {
  const size_t n = aitext::buildPage(question, note, body, nullptr, 0);
  std::string out(n + 1, '\0');
  aitext::buildPage(question, note, body, out.data(), out.size());
  out.resize(n);
  return out;
}

void addLine(std::string& body, const char* line) {
  if (line == nullptr || line[0] == '\0') return;
  body += line;
  body += '\n';
}

void addStep(std::string& body, const int number, const char* line) {
  char head[8];
  std::snprintf(head, sizeof(head), "%d. ", number);
  body += head;
  body += line;
  body += '\n';
}

}  // namespace

AiPageActivity::AiPageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string text,
                               std::string lead, std::string section)
    : Activity("AiPage", renderer, mappedInput),
      text(std::move(text)),
      lead(std::move(lead)),
      section(std::move(section)) {}

std::unique_ptr<AiPageActivity> AiPageActivity::forExchange(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                            const int exchange) {
  const GeminiChat* chat = AiRun::chat();
  if (chat == nullptr || exchange < 0 || exchange >= chat->n) return nullptr;
  const GeminiExchange& ex = chat->ex[exchange];
  const bool failed = (ex.flags & GEM_EX_FAILED) != 0;
  char lead[GEM_LABEL_MAX + 48];
  if (failed) {
    std::snprintf(lead, sizeof(lead), "%s", tr(STR_AI_NO_ANSWER));
  } else {
    std::snprintf(lead, sizeof(lead), tr(STR_AI_ANSWERED_BY), ex.label);
  }
  if ((ex.flags & GEM_EX_TOPIC) && exchange > 0) {
    char marked[sizeof(lead) + 32];
    std::snprintf(marked, sizeof(marked), tr(STR_AI_NEW_TOPIC_MARK), lead);
    std::snprintf(lead, sizeof(lead), "%s", marked);
  }
  auto page = makeUniqueNoThrow<AiPageActivity>(renderer, mappedInput,
                                                buildText(ex.q, failed ? ex.a : nullptr, failed ? "" : ex.a), lead,
                                                failed ? tr(STR_AI_NO_ANSWER) : ex.label);
  if (!page) LOG_ERR("AI", "OOM: answer page");
  return page;
}

std::unique_ptr<AiPageActivity> AiPageActivity::forHelp(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  const AiRun::KeyInfo& key = AiRun::keyInfo();
  std::string body;
  body.reserve(2048);
  char line[320];
  switch (key.state) {
    case AiRun::KeyState::Ok:
      std::snprintf(line, sizeof(line), tr(STR_AI_KEY_LINE_FOUND), key.path, key.length);
      addLine(body, line);
      break;
    case AiRun::KeyState::Refused:
      addLine(body, tr(STR_AI_KEY_LINE_REFUSED));
      break;
    case AiRun::KeyState::Unreadable:
      addLine(body, tr(STR_AI_KEY_LINE_UNREADABLE));
      break;
    case AiRun::KeyState::None:
    case AiRun::KeyState::NotRead:
      addLine(body, tr(STR_AI_KEY_LINE_NONE));
      break;
  }
  // gemini.txt's own reading (gemini.h's Key info line: the key's length and the settings, never the
  // key), with any warning about a line it ignored.
  if (key.path[0] && key.line[0]) {
    std::snprintf(line, sizeof(line), tr(STR_AI_FILE_LINE), key.line);
    addLine(body, line);
  }
  const char* model = key.model[0] ? key.model : GEM_DEFAULT_MODEL;
  const char* fallback = key.usable ? key.fallback : GEM_LITE_MODEL;
  char a[GEM_LABEL_MAX];
  char b[GEM_LABEL_MAX];
  geminiModelLabel(model, a, sizeof(a));
  geminiModelLabel(fallback, b, sizeof(b));
  if (fallback[0]) {
    std::snprintf(line, sizeof(line), tr(STR_AI_MODEL_LINE), a, model, b, fallback);
  } else {
    std::snprintf(line, sizeof(line), tr(STR_AI_MODEL_LINE_ALONE), a, model);
  }
  addLine(body, line);
  addLine(body, key.lookup ? tr(STR_AI_LOOKUP_ON) : tr(STR_AI_LOOKUP_OFF));
  addLine(body, AiRun::retryTimeouts() ? tr(STR_AI_RETRY_ON) : tr(STR_AI_RETRY_OFF));
  if (AiRun::quotaNote(line, sizeof(line)) > 0) addLine(body, line);
  addLine(body, tr(STR_AI_HELP_INTRO));
  addStep(body, 1, tr(STR_AI_HELP_1));
  addStep(body, 2, tr(STR_AI_HELP_2));
  addStep(body, 3, tr(STR_AI_HELP_3));
  addStep(body, 4, tr(STR_AI_HELP_4));
  addStep(body, 5, tr(STR_AI_HELP_5));
  addStep(body, 6, tr(STR_AI_HELP_6));
  addLine(body, tr(STR_AI_HELP_MODELS));
  addLine(body, tr(STR_AI_HELP_WIFI));
  addLine(body, tr(STR_AI_HELP_PRIVACY));
  auto page = makeUniqueNoThrow<AiPageActivity>(
      renderer, mappedInput, buildText(tr(STR_AI_HELP_TITLE), nullptr, body.c_str()), "", tr(STR_AI_KEY_HELP));
  if (!page) LOG_ERR("AI", "OOM: help page");
  return page;
}

void AiPageActivity::onEnter() {
  Activity::onEnter();
  buffer = HalMemory::allocatePsram(text.size() + 2);
  if (!buffer || !topic.make() || !layout.make()) {
    LOG_ERR("AI", "OOM: page (%u bytes of text)", static_cast<unsigned>(text.size()));
    failed = true;
  } else {
    char* bytes = reinterpret_cast<char*>(buffer.get());
    std::memcpy(bytes, text.data(), text.size());
    bytes[text.size()] = '\0';
    const gd::GpError err = topic->parse(bytes, text.size());
    if (err != gd::GpError::None) {
      LOG_ERR("AI", "Page refused (%d, line %d)", static_cast<int>(err), topic->errorLine());
      failed = true;
    } else {
      // The layout measures with the fonts: the render task's, so under its lock.
      RenderLock lock(*this);
      const gd::RendererMetrics metrics(renderer, gd::pageFonts());
      screens = gd::layoutPage(*topic, 0, lead.empty() ? nullptr : lead.c_str(), metrics, gd::Geometry{}, nullptr,
                               layout.get());
      const char* parts[2] = {tr(STR_AI), section.c_str()};
      gd::breadcrumb(parts, 2, metrics, gd::Font::Crumb, gd::draw::crumbWidth(), crumb, sizeof(crumb));
      if (screens <= 0) failed = true;
      if (layout->truncated()) LOG_ERR("AI", "Page cut at %d screens", screens);
    }
  }
  // The parsed copy is what is drawn now.
  text.clear();
  text.shrink_to_fit();
  halfPending = true;
  ready.store(true);
  requestUpdate();
}

bool AiPageActivity::handleForcedRefresh() {
  halfPending = true;
  requestUpdate();
  return true;
}

void AiPageActivity::turn(const int dir) {
  const int next = sub.load() + dir;
  if (next < 0 || next >= screens) return;
  sub.store(next);
  requestUpdate();
}

void AiPageActivity::loop() {
  if (!ready.load()) return;
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    switch (failed ? gd::draw::BarButton::Middle : gd::draw::barButtonAt(x, y)) {
      case gd::draw::BarButton::Prev:
        turn(-1);
        return;
      case gd::draw::BarButton::Middle:
        finish();
        return;
      case gd::draw::BarButton::Next:
        turn(1);
        return;
      case gd::draw::BarButton::None:
        return;
    }
  }
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    turn(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right && !mappedInput.wasBackGesture()) {
    turn(-1);
    return;
  }
  // The edge keys: the left one held goes back once it is let go (at the threshold, the held key
  // would reach the list).
  if (mappedInput.wasLongPressed(Button::Up, KEY_LONG_PRESS_MS)) {
    upOnRelease = true;
    return;
  }
  if (upOnRelease) {
    if (mappedInput.isPressed(Button::Up)) return;
    upOnRelease = false;
    finish();
    return;
  }
  if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::PageForward) ||
      mappedInput.wasReleased(Button::Right)) {
    turn(1);
  } else if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::PageBack) ||
             mappedInput.wasReleased(Button::Left)) {
    turn(-1);
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    finish();
  }
}

void AiPageActivity::render(RenderLock&&) {
  if (!ready.load()) return;
  renderer.clearScreen();
  if (failed) {
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_MEMORY_ERROR));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  const int shown = sub.load();
  gd::draw::drawCrumb(renderer, crumb, false);
  gd::draw::PageView view;
  view.layout = layout.get();
  view.screen = shown;
  view.fonts = &gd::pageFonts();
  view.missingLabel = "";
  gd::draw::drawPageBody(renderer, view);
  gd::draw::BarView bar;
  char count[16];
  std::snprintf(count, sizeof(count), "%d/%d", shown + 1, screens);
  bar.prevLabel = tr(STR_GD_PREV);
  bar.prevEnabled = shown > 0;
  bar.middleLabel = tr(STR_GD_BACK);
  bar.middleDetail = count;
  bar.nextLabel = tr(STR_GD_NEXT);
  bar.nextEnabled = shown + 1 < screens;
  gd::draw::drawBar(renderer, bar);
  // HALF on entry and every refresh-frequency-th screen (the reader's own counter), FAST between.
  const bool half = halfPending.exchange(false) || rendersUntilHalf <= 1;
  if (half) {
    rendersUntilHalf = SETTINGS.getRefreshFrequency();
  } else {
    rendersUntilHalf--;
  }
  renderer.displayBuffer(half ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
