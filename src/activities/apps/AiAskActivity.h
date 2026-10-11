#pragma once

#include <freertos/FreeRTOS.h>
#include <gemini.h>

#include <atomic>
#include <string>

#include "activities/Activity.h"
#include "network/AiRun.h"

// Apps > AI: one question on its way (pushed by AiActivity, which opens the answer when it finishes).
//   Join     a saved Wi-Fi network through WifiSelectionActivity's auto-connect (the last network,
//            then any saved one in range; the network list only when none is). Not joined: the
//            question goes into the list as failed ("No Wi-Fi joined"), nothing sent.
//   Working  AiRun::ask on the loop task: the Wikipedia lookup, then Gemini (up to 150 s a request,
//            the ladder's retries). The screen shows the question, the stage, the model and the
//            seconds; Cancel (a tap on it, Back, the header's arrow or the left key) ends it at the
//            next slice. The input is pumped from AiRun's tick, as OPDS downloads do.
// Wi-Fi is turned off as soon as the question ends (RadioPower::off()) when this screen turned it on;
// a station already up when the question was asked is left as it was. Result: a MenuResult whose
// action is the chat exchange to open, or -1 (cancelled).
class AiAskActivity final : public Activity {
 public:
  AiAskActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string question);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::Working; }
  bool preventAutoSleep() override { return state != State::Done; }

 private:
  enum class State : uint8_t { Joining, Working, Done };
  // What render() draws, copied under frameLock (the loop task is inside AiRun::ask meanwhile).
  struct Frame {
    char stage[GEM_LABEL_MAX + 160] = {};
    char model[GEM_LABEL_MAX] = {};
    uint32_t seconds = 0;
    uint32_t waitSeconds = 0;  // a retry's wait left (0 = not waiting)
    bool cancelling = false;
  };

  static bool onTick(void* ctx);
  static void onChanged(const AiRun::Progress& progress, void* ctx);
  void publish(const AiRun::Progress* progress);
  void runQuestion();
  void finishWith(int exchange);
  bool cancelTapped(int x, int y) const;

  std::string question;
  State state = State::Joining;
  bool cancelRequested = false;
  bool goHomeAfter = false;
  bool radioStarted = false;  // this screen started the join (so it turns Wi-Fi off after)
  uint32_t startMs = 0;
  uint32_t lastSeconds = 0;
  AiRun::Progress last{};  // the newest progress (loop task); "" until AiRun's first stage
  Frame shared;          // loop -> render, under frameLock
  portMUX_TYPE frameLock = portMUX_INITIALIZER_UNLOCKED;
  // The Cancel button, as render() drew it (render -> loop; a torn read misses one tap at worst).
  std::atomic<int> cancelTop{0};
  std::atomic<int> cancelBottom{0};
  bool firstFrame = true;  // render task only: the first frame is a HALF refresh (the keyboard leaves)
};
