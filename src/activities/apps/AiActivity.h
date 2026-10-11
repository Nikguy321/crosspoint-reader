#pragma once

#include <gemini.h>

#include <string>

#include "activities/UiListActivity.h"

// Apps > AI (X4 Pro): Google's Gemini, plain text, with a Wikipedia lookup before each question. The
// rules are lib/Gemini's and lib/WikiLookup's, shared with the WiPhone firmware; the device half is
// network/AiRun (the key file, the topic, one question) and network/AiHttps (the wire).
//
// This list is the app's home: Ask a question; New topic; Retry if it times out (On / Off, saved
// with the settings); Key info & help; then this session's questions, newest first, a failed one
// marked. Ask opens the keyboard at once; the question goes to AiAskActivity (Wi-Fi joined for it and
// turned off after), and its answer opens in AiPageActivity, whose Back returns here. With no key,
// Ask and the app's first open show the help page (how to get a free key and where to put it).
// Leaving after Wi-Fi was used reboots to the Apps list (silently, as every network activity's exit
// does, to free the heap the Wi-Fi driver fragmented); the questions are RAM only.
class AiActivity final : public UiListActivity {
 public:
  explicit AiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

  // AiAskActivity started the radio, or asked on a station already up: leaving the app reboots.
  static void noteRadioUsed() { radioUsed = true; }

 private:
  enum Row : uint8_t { ROW_ASK, ROW_TOPIC, ROW_RETRY, ROW_KEY, ROW_FIXED };
  static constexpr int MAX_ROWS = ROW_FIXED + GEM_CHAT_MAX;

  int listCount() const override { return rowCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  void refreshRows();
  void askQuestion();
  void sendQuestion(const std::string& question);
  void openExchange(int exchange);
  void openHelp();

  freeink::ui::ListItem rows[MAX_ROWS]{};
  char subtitles[MAX_ROWS][48] = {};
  char keySubtitle[48] = {};
  const char* topicSubtitle = "";
  int rowCount = ROW_FIXED;
  bool helpShownOnce = false;
  bool leavingToApps = false;

  static bool radioUsed;
};
