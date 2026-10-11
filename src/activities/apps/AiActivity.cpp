#include "AiActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "AiAskActivity.h"
#include "AiPageActivity.h"
#include "AppsActivity.h"
#include "LiveSleep.h"
#include "SilentRestart.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "network/AiRun.h"

namespace fui = freeink::ui;

bool AiActivity::radioUsed = false;

AiActivity::AiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Ai", renderer, mappedInput) {}

void AiActivity::onEnter() {
  UiListActivity::onEnter();
  AiRun::reloadKey();  // a key put on the card since the last open works now
  refreshRows();
  // The first open with no key goes straight to how to get one.
  if (!helpShownOnce && !AiRun::keyInfo().usable) {
    helpShownOnce = true;
    openHelp();
  }
}

void AiActivity::onExit() {
  // Every network activity leaves by rebooting, to free the heap the Wi-Fi driver fragmented (the
  // questions are RAM only). Not into sleep: deep sleep resets anyway, and the live sleep screen
  // takes the radio over.
  if (radioUsed && !deepSleepStarting() && !liveSleepActive()) {
    if (leavingToApps) {
      silentRestartToApps();
    } else {
      silentRestart();
    }
  }
  UiListActivity::onExit();
}

const char* AiActivity::headerTitle() const { return tr(STR_AI); }

void AiActivity::refreshRows() {
  RenderLock lock(*this);
  const AiRun::KeyInfo& key = AiRun::keyInfo();
  rows[ROW_ASK].label = tr(STR_AI_ASK);
  rows[ROW_ASK].subtitle = key.usable ? tr(STR_AI_ASK_DESC) : tr(STR_AI_KEY_NONE);
  rows[ROW_TOPIC].label = tr(STR_AI_NEW_TOPIC);
  rows[ROW_TOPIC].subtitle = topicSubtitle[0] ? topicSubtitle : tr(STR_AI_NEW_TOPIC_DESC);
  rows[ROW_RETRY].label = tr(STR_AI_RETRY);
  rows[ROW_RETRY].subtitle = AiRun::retryTimeouts() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rows[ROW_KEY].label = tr(STR_AI_KEY_HELP);
  switch (key.state) {
    case AiRun::KeyState::Ok:
      std::snprintf(keySubtitle, sizeof(keySubtitle), tr(STR_AI_KEY_FOUND), key.length);
      break;
    case AiRun::KeyState::Refused:
      std::snprintf(keySubtitle, sizeof(keySubtitle), "%s", tr(STR_AI_KEY_REFUSED));
      break;
    case AiRun::KeyState::Unreadable:
      std::snprintf(keySubtitle, sizeof(keySubtitle), "%s", tr(STR_AI_KEY_UNREADABLE));
      break;
    case AiRun::KeyState::None:
    case AiRun::KeyState::NotRead:
      std::snprintf(keySubtitle, sizeof(keySubtitle), "%s", tr(STR_AI_KEY_NONE));
      break;
  }
  rows[ROW_KEY].subtitle = keySubtitle;

  // This session's questions, newest first: the model that answered, or why there is no answer.
  rowCount = ROW_FIXED;
  const GeminiChat* chat = AiRun::chat();
  for (int i = chat != nullptr ? chat->n - 1 : -1; i >= 0 && rowCount < MAX_ROWS; i--) {
    const GeminiExchange& ex = chat->ex[i];
    fui::ListItem& row = rows[rowCount];
    row = fui::ListItem{};
    row.label = ex.q;
    if (ex.flags & GEM_EX_FAILED) {
      std::snprintf(subtitles[rowCount], sizeof(subtitles[rowCount]), "%s: %s", tr(STR_AI_NO_ANSWER), ex.a);
    } else {
      std::snprintf(subtitles[rowCount], sizeof(subtitles[rowCount]), tr(STR_AI_ANSWERED_BY), ex.label);
    }
    row.subtitle = subtitles[rowCount];
    rowCount++;
  }
  for (int i = 0; i < rowCount; i++) rows[i].actionValue = static_cast<int16_t>(i);
}

void AiActivity::openHelp() {
  auto page = AiPageActivity::forHelp(renderer, mappedInput);
  if (!page) return;  // logged
  startActivityForResult(std::move(page), [this](const ActivityResult&) { refreshRows(); });
}

void AiActivity::openExchange(const int exchange) {
  auto page = AiPageActivity::forExchange(renderer, mappedInput, exchange);
  if (!page) return;  // logged
  startActivityForResult(std::move(page), [this](const ActivityResult&) { refreshRows(); });
}

void AiActivity::askQuestion() {
  // The keyboard at once; the key file is read again just before the question goes.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_AI_ASK_TITLE), "",
                                                           static_cast<size_t>(GEM_Q_CAP), InputType::Text);
  if (!keyboard) {
    LOG_ERR("AI", "OOM: keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* typed = std::get_if<KeyboardResult>(&result.data);
    if (typed == nullptr) return;
    size_t begin = typed->text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return;
    const size_t end = typed->text.find_last_not_of(" \t\r\n");
    sendQuestion(typed->text.substr(begin, end - begin + 1));
  });
}

void AiActivity::sendQuestion(const std::string& question) {
  auto ask = makeUniqueNoThrow<AiAskActivity>(renderer, mappedInput, question);
  if (!ask) {
    LOG_ERR("AI", "OOM: question screen");
    return;
  }
  topicSubtitle = "";
  startActivityForResult(std::move(ask), [this](const ActivityResult& result) {
    refreshRows();
    const auto* done = std::get_if<MenuResult>(&result.data);
    nav.selected = ROW_ASK;
    if (done != nullptr && done->action >= 0) openExchange(done->action);
  });
}

void AiActivity::activateIndex(const int index) {
  // The row leaves this screen (or changes it): a lingering flash would gray an unrelated element.
  app.clearTapFlash();
  nav.selected = index;
  if (index < 0 || index >= rowCount) return;
  switch (index) {
    case ROW_ASK:
      AiRun::reloadKey();
      if (AiRun::keyInfo().usable) {
        askQuestion();
      } else {
        refreshRows();
        openHelp();
      }
      return;
    case ROW_TOPIC:
      topicSubtitle = AiRun::newTopic() ? tr(STR_AI_NEW_TOPIC_DONE) : tr(STR_AI_NO_TOPIC);
      refreshRows();
      requestUpdate();
      return;
    case ROW_RETRY:
      AiRun::setRetryTimeouts(!AiRun::retryTimeouts());
      refreshRows();
      requestUpdate();
      return;
    case ROW_KEY:
      AiRun::reloadKey();
      refreshRows();
      openHelp();
      return;
    default: {
      // Question rows are newest first.
      const GeminiChat* chat = AiRun::chat();
      const int exchange = chat != nullptr ? chat->n - 1 - (index - ROW_FIXED) : -1;
      if (exchange >= 0) openExchange(exchange);
      return;
    }
  }
}

// Not finish(): with nothing under this screen it would go Home with no row selected.
void AiActivity::onBackButton() {
  leavingToApps = true;
  AppsActivity::selectOnNextOpen(AppsActivity::App::Ai);
  activityManager.goToApps();
}

void AiActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rows;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
