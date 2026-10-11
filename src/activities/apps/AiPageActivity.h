#pragma once

#include <HalMemory.h>

#include <atomic>
#include <memory>
#include <string>

#include "GuideStore.h"
#include "activities/Activity.h"

// Apps > AI: one page of text - an answer (or why there is none), or Key info & help - drawn the way
// the survival guide draws a page: its layout (lib/Guide's GdLayout: the question as the title, the
// answer's paragraphs, "- " bullets and numbered steps, a boxed note) in the reader's NotoSans 12,
// screen by screen, with the guide's strip ("AI / FLASH") and bar (< PREV | BACK n/m | NEXT >).
// The text is built by aitext::buildPage, laid out once in onEnter (under the render lock: it measures
// with the fonts) and only drawn after.
//
// Touch: the bar, a swipe left / right turns, the left-edge swipe is Back. X4 Pro keys: right / left
// turn; the left key held goes back (on release). Back returns to the app's list.
class AiPageActivity final : public Activity {
 public:
  // text: the page (aitext::buildPage); lead: the small line above the title ("Answered by Flash");
  // section: the strip's second part ("Flash", "No answer", "Help").
  AiPageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string text, std::string lead,
                 std::string section);

  // An exchange of AiRun::chat(); nullptr when it cannot be built (logged).
  static std::unique_ptr<AiPageActivity> forExchange(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     int exchange);
  // Key info & help (AiRun::keyInfo() as last read).
  static std::unique_ptr<AiPageActivity> forHelp(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleForcedRefresh() override;

 private:
  void turn(int dir);

  std::string text;
  std::string lead;
  std::string section;
  HalMemory::PsramBuffer buffer;  // the text, parsed in place (one spare byte)
  gd::store::Box<gd::TopicText> topic;
  gd::store::Box<gd::PageLayout> layout;
  bool failed = false;
  int screens = 0;
  std::atomic<int> sub{0};
  std::atomic<bool> ready{false};
  std::atomic<bool> halfPending{true};
  int rendersUntilHalf = 0;  // render task only
  bool upOnRelease = false;
  char crumb[96] = {};
};
