#pragma once

// Input injection sequencers for the bench console. Pure C++ so the host tests
// can drive them with a fake clock; HalGPIO owns one of each and feeds them
// from its update().

#include <cstdint>

namespace bench {

// Plays one button press through InputManager's button hook: the mask is OR'd
// into the raw sample, so debounce, edges, held time and the power-button logic
// all see a physical press. The hold is timed from the committed press edge.
class KeyInjector {
 public:
  static constexpr uint32_t EDGE_TIMEOUT_MS = 1000;

  // presses > 1 repeats the press after gapMs released (a double-click).
  void start(uint8_t buttonIndex, uint32_t holdMs, uint8_t presses = 1, uint32_t gapMs = 0);
  // Call after every InputManager::update() with that button's committed level.
  void onUpdate(uint32_t now, bool committedPressed);
  // Raw-sample mask for the button hook.
  uint8_t mask() const { return phase == Phase::Asserting || phase == Phase::Holding ? bit : 0; }
  uint8_t buttonIndex() const { return index; }
  bool busy() const { return phase != Phase::Idle && phase != Phase::Done; }
  // Done = the last release edge was delivered and at least one further update
  // ran, so every consumer of the release frame has seen it.
  bool done() const { return phase == Phase::Done; }
  // Every press committed (false after an edge timeout).
  bool pressDelivered() const { return deliveredPresses == presses; }
  // At least one press edge reached InputManager.
  bool anyDelivered() const { return deliveredPresses > 0; }
  // Committed press edge to committed release edge of the last press, as the
  // firmware measured it (a blocked main loop stretches it like a real press).
  uint32_t heldMs() const { return actualHeldMs; }
  // End the gesture at the next update: an asserted press is released.
  void finishSoon();
  void clear() { phase = Phase::Idle; }

 private:
  enum class Phase : uint8_t { Idle, Asserting, Holding, Releasing, Released, Gap, Done };
  Phase phase = Phase::Idle;
  uint8_t index = 0;
  uint8_t bit = 0;
  uint8_t presses = 1;
  uint8_t pressesLeft = 0;
  uint8_t deliveredPresses = 0;
  uint32_t holdMs = 0;
  uint32_t gapMs = 0;
  uint32_t phaseStart = 0;
  bool phaseStartValid = false;
  uint32_t actualHeldMs = 0;
};

// Synthesizes a finger on the touch panel (and the capacitive Home key) with the
// same per-frame event contract and classification thresholds as the SDK's
// InputManager touch machine. Coordinates are panel-native pixels; the query
// functions return the SDK's normalized 0..1 panel-frame values.
class TouchInjector {
 public:
  // Thresholds mirrored from freeink-sdk InputManager.h (private there, so they
  // cannot be static_asserted): TOUCH_TAP_SLOP_PX, TOUCH_SWIPE_MIN_PX,
  // TOUCH_TAP_RELEASE_SLOP_PX, TOUCH_SWIPE_MAX_MS, TOUCH_LONG_PRESS_MS
  // (InputManager.h:450-457) and HOME_KEY_LONG_PRESS_MS (:398).
  static constexpr int TAP_SLOP_PX = 28;
  static constexpr int SWIPE_MIN_PX = 60;
  static constexpr int TAP_RELEASE_SLOP_PX = SWIPE_MIN_PX - 1;
  static constexpr uint32_t SWIPE_MAX_MS = 700;
  static constexpr uint32_t LONG_PRESS_MS = 500;
  static constexpr uint32_t HOME_LONG_PRESS_MS = 700;

  void setPanelSize(int width, int height) {
    panelW = width > 0 ? width : 1;
    panelH = height > 0 ? height : 1;
  }
  // A contact from (x1,y1) moving linearly to (x2,y2), released after holdMs.
  void startContact(int x1, int y1, int x2, int y2, uint32_t holdMs);
  // taps > 1 repeats the tap after gapMs released (a double-tap).
  void startHomeKey(uint32_t holdMs, uint8_t taps = 1, uint32_t gapMs = 0);
  // Advance one input frame. Call once per HalGPIO::update().
  void onUpdate(uint32_t now);
  bool busy() const { return script != Script::Idle && !done(); }
  bool done() const { return script != Script::Idle && phase == Phase::Done; }
  // The first touch-down (or Home press) frame has been played.
  bool delivered() const { return script != Script::Idle && startedOnce; }
  // Duration of the last contact or Home press as delivered.
  uint32_t heldMs() const { return actualHeldMs; }
  // End the gesture at the next update: a held contact lifts (a real release
  // frame), a gesture not yet started is dropped, no further repeats run.
  void finishSoon();
  void clear();

  // SDK-equivalent frame queries.
  bool tap(float& nx, float& ny) const;
  bool down(float& nx, float& ny) const;
  bool released() const { return releasedEvent && !suppressed; }
  bool tapCandidate(uint32_t now, float& nx, float& ny, unsigned long& heldMs) const;
  bool heldAt(float& nx, float& ny) const;
  bool longPress(float& nx, float& ny) const;
  bool swipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const;
  unsigned long lastHeldMs() const { return latchedHeldMs; }
  bool activity() const;
  bool homePressed() const { return homePressEvent; }
  bool homeTapped() const { return homeTapEvent; }
  bool homeLongPressed() const { return homeLongEvent; }
  bool contactActive() const { return pressed; }
  void suppress() {
    if (pressed || releasedEvent) suppressed = true;
  }

 private:
  enum class Script : uint8_t { Idle, Contact, HomeKey };
  enum class Phase : uint8_t { Start, Holding, Released, Gap, Done };

  void normalize(int x, int y, float& nx, float& ny) const;
  void track(int x, int y);

  Script script = Script::Idle;
  Phase phase = Phase::Done;
  int panelW = 800;
  int panelH = 480;
  int fromX = 0, fromY = 0, toX = 0, toY = 0;
  uint32_t holdMs = 0;
  uint32_t downAt = 0;
  uint8_t tapsLeft = 0;
  uint32_t gapMs = 0;
  uint32_t gapStart = 0;
  bool startedOnce = false;
  uint32_t actualHeldMs = 0;

  bool pressed = false;
  bool pressedEvent = false;
  bool releasedEvent = false;
  bool longPressEvent = false;
  bool longFired = false;
  bool suppressed = false;
  bool movedBeyondTapSlop = false;
  bool movedBeyondReleaseSlop = false;
  int downX = 0, downY = 0, curX = 0, curY = 0;
  unsigned long latchedHeldMs = 0;

  bool homeDown = false;
  bool homePressEvent = false;
  bool homeTapEvent = false;
  bool homeLongEvent = false;
  bool homeLongFired = false;
};

// Whether a USB host is attached, from the SOF-watchdog "plugged" reading. A
// host appears at once; it is gone only after absentMs of continuous absence
// AND at least MIN_ABSENT_READINGS readings in a row, so neither a transient SOF
// glitch nor one stale reading after a long main-loop stall counts as a
// disconnect.
class HostPresence {
 public:
  static constexpr uint32_t DEFAULT_ABSENT_MS = 2000;
  static constexpr uint8_t MIN_ABSENT_READINGS = 3;
  explicit HostPresence(uint32_t absentMs = DEFAULT_ABSENT_MS) : absentMs(absentMs) {}
  void update(uint32_t now, bool plugged);
  bool present() const { return isPresent; }

 private:
  uint32_t absentMs;
  uint32_t absentSince = 0;
  uint8_t absentReadings = 0;
  bool isPresent = false;
};

// When to answer a queued KEY / TAP / LONGTAP / SWIPE. The injectors are driven
// by the clock on every main-loop pass, so they always finish unless the loop
// stops; a blocked loop only makes the answer late (reported, never an error,
// and the injection is never cut short). An error needs the gesture to be
// unfinished after the deadline for STUCK_PASSES loop passes AND STUCK_MS.
class InputWaiter {
 public:
  static constexpr uint32_t MARGIN_MS = 3000;
  static constexpr uint32_t STUCK_MS = 2500;  // > 2 x KeyInjector::EDGE_TIMEOUT_MS
  static constexpr uint16_t STUCK_PASSES = 20;
  enum class Verdict : uint8_t { Wait, Ok, ErrNoEdge, ErrTimeout };

  void start(uint32_t now, uint32_t holdMs, uint32_t settleMs, bool needsKeyEdge);
  // Once per main-loop pass after the injectors advanced. pressDelivered: the
  // key's press edges all committed (only read when needsKeyEdge).
  Verdict poll(uint32_t now, bool injectionDone, bool pressDelivered);
  bool late() const { return isLate; }

 private:
  uint32_t deadline = 0;
  uint32_t settleMs = 0;
  uint32_t settleUntil = 0;
  uint32_t lateSince = 0;
  uint16_t latePasses = 0;
  bool needsKeyEdge = false;
  bool settling = false;
  bool isLate = false;
};

// Logical screen pixel (the SHOT image / renderer frame for `orientation`,
// GfxRenderer::Orientation order: 0 Portrait, 1 LandscapeClockwise,
// 2 PortraitInverted, 3 LandscapeCounterClockwise) to panel-native pixel.
// Mirrors GfxRenderer's rotateCoordinates.
void logicalToPanel(int orientation, int x, int y, int panelW, int panelH, int& px, int& py);

}  // namespace bench
