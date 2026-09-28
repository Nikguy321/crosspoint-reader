#include "BenchInjection.h"

namespace bench {

namespace {
int absInt(const int v) { return v < 0 ? -v : v; }
}  // namespace

// --- KeyInjector -------------------------------------------------------------

void KeyInjector::start(const uint8_t buttonIndex, const uint32_t hold, const uint8_t count, const uint32_t gap) {
  index = buttonIndex;
  bit = static_cast<uint8_t>(1u << (buttonIndex & 7u));
  holdMs = hold;
  presses = count > 0 ? count : 1;
  pressesLeft = presses;
  gapMs = gap;
  phase = Phase::Asserting;
  phaseStartValid = false;
  deliveredPresses = 0;
  actualHeldMs = 0;
}

void KeyInjector::finishSoon() {
  pressesLeft = 1;
  if (phase == Phase::Asserting || phase == Phase::Holding) {
    phase = Phase::Releasing;
    phaseStartValid = false;
  } else if (phase == Phase::Gap) {
    phase = Phase::Released;
  }
}

void KeyInjector::onUpdate(const uint32_t now, const bool committedPressed) {
  if (!phaseStartValid) {
    phaseStart = now;
    phaseStartValid = true;
  }
  switch (phase) {
    case Phase::Asserting:
      // The update that first samples the mask only starts the debounce; the
      // press commits on a later one, exactly as a physical contact does.
      if (committedPressed) {
        ++deliveredPresses;
        phase = Phase::Holding;
        phaseStart = now;
      } else if (now - phaseStart >= EDGE_TIMEOUT_MS) {
        phase = Phase::Releasing;
        phaseStart = now;
      }
      break;
    case Phase::Holding:
      if (now - phaseStart >= holdMs) {
        phase = Phase::Releasing;
        actualHeldMs = now - phaseStart;  // provisional: completed at the release edge
        phaseStart = now;
      }
      break;
    case Phase::Releasing:
      // Released on the update that commits the release edge. A physical
      // button still held keeps the level up; give up waiting after the timeout.
      if (!committedPressed || now - phaseStart >= EDGE_TIMEOUT_MS) {
        if (deliveredPresses > 0) actualHeldMs += now - phaseStart;
        phase = Phase::Released;
      }
      break;
    case Phase::Released:
      if (pressesLeft > 1) {
        --pressesLeft;
        phase = Phase::Gap;
        phaseStart = now;
      } else {
        phase = Phase::Done;
      }
      break;
    case Phase::Gap:
      if (now - phaseStart >= gapMs) {
        phase = Phase::Asserting;
        phaseStart = now;
      }
      break;
    case Phase::Idle:
    case Phase::Done:
      break;
  }
}

// --- TouchInjector -----------------------------------------------------------

void TouchInjector::startContact(const int x1, const int y1, const int x2, const int y2, const uint32_t hold) {
  clear();
  script = Script::Contact;
  phase = Phase::Start;
  fromX = x1;
  fromY = y1;
  toX = x2;
  toY = y2;
  holdMs = hold;
  tapsLeft = 1;
}

void TouchInjector::startHomeKey(const uint32_t hold, const uint8_t taps, const uint32_t gap) {
  clear();
  script = Script::HomeKey;
  phase = Phase::Start;
  holdMs = hold;
  tapsLeft = taps > 0 ? taps : 1;
  gapMs = gap;
}

void TouchInjector::finishSoon() {
  tapsLeft = 1;
  switch (phase) {
    case Phase::Start:
      if (!startedOnce) {
        clear();  // nothing was delivered: drop it
      } else {
        phase = Phase::Done;  // between repeats: nothing is held
      }
      break;
    case Phase::Holding:
      holdMs = 0;  // the next update lifts the finger with a real release frame
      break;
    case Phase::Gap:
      phase = Phase::Done;
      break;
    case Phase::Released:
    case Phase::Done:
      break;
  }
}

void TouchInjector::clear() {
  script = Script::Idle;
  phase = Phase::Done;
  pressed = false;
  pressedEvent = releasedEvent = longPressEvent = false;
  longFired = suppressed = false;
  homeDown = false;
  homePressEvent = homeTapEvent = homeLongEvent = homeLongFired = false;
  startedOnce = false;
  tapsLeft = 0;
  actualHeldMs = 0;
}

void TouchInjector::track(const int x, const int y) {
  curX = x;
  curY = y;
  const int dx = absInt(curX - downX);
  const int dy = absInt(curY - downY);
  if (dx > TAP_SLOP_PX || dy > TAP_SLOP_PX) movedBeyondTapSlop = true;
  if (dx > TAP_RELEASE_SLOP_PX || dy > TAP_RELEASE_SLOP_PX) movedBeyondReleaseSlop = true;
}

void TouchInjector::onUpdate(const uint32_t now) {
  // One-shot events last exactly one frame, as InputManager::update() clears
  // its own before sampling. The suppression latch ends once the contact is over.
  pressedEvent = releasedEvent = longPressEvent = false;
  homePressEvent = homeTapEvent = homeLongEvent = false;
  if (!pressed) {
    suppressed = false;
    longFired = false;
  }

  if (script == Script::Contact) {
    switch (phase) {
      case Phase::Start:
        startedOnce = true;
        pressed = true;
        pressedEvent = true;
        downAt = now;
        downX = curX = fromX;
        downY = curY = fromY;
        movedBeyondTapSlop = movedBeyondReleaseSlop = false;
        phase = Phase::Holding;
        break;
      case Phase::Holding: {
        const uint32_t elapsed = now - downAt;
        if (elapsed >= holdMs) {
          // Last contact sample sits on the end point, then the finger lifts.
          track(toX, toY);
          releasedEvent = true;
          latchedHeldMs = elapsed;
          actualHeldMs = elapsed;
          pressed = false;
          phase = Phase::Released;
        } else {
          const float t = static_cast<float>(elapsed) / static_cast<float>(holdMs);
          track(fromX + static_cast<int>(static_cast<float>(toX - fromX) * t),
                fromY + static_cast<int>(static_cast<float>(toY - fromY) * t));
        }
        break;
      }
      case Phase::Released:
      case Phase::Gap:
        phase = Phase::Done;
        break;
      case Phase::Done:
        break;
    }
    if (pressed && !movedBeyondTapSlop && !longFired && !suppressed && now - downAt >= LONG_PRESS_MS) {
      longFired = true;
      longPressEvent = true;
    }
  } else if (script == Script::HomeKey) {
    switch (phase) {
      case Phase::Start:
        startedOnce = true;
        homeDown = true;
        homePressEvent = true;
        homeLongFired = false;
        downAt = now;
        phase = Phase::Holding;
        break;
      case Phase::Holding:
        // Long press fires while held and suppresses the release tap.
        if (!homeLongFired && now - downAt >= HOME_LONG_PRESS_MS) {
          homeLongEvent = true;
          homeLongFired = true;
        }
        if (now - downAt >= holdMs) {
          if (!homeLongFired) homeTapEvent = true;
          homeDown = false;
          actualHeldMs = now - downAt;
          phase = Phase::Released;
        }
        break;
      case Phase::Released:
        if (tapsLeft > 1) {
          --tapsLeft;
          gapStart = now;
          phase = Phase::Gap;
        } else {
          phase = Phase::Done;
        }
        break;
      case Phase::Gap:
        if (now - gapStart >= gapMs) phase = Phase::Start;
        break;
      case Phase::Done:
        break;
    }
  }
}

void TouchInjector::normalize(const int x, const int y, float& nx, float& ny) const {
  // Pixel centres, so GfxRenderer::tapToLogical's (int)(n * panel) lands back
  // on exactly this pixel.
  nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(panelW);
  ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(panelH);
}

bool TouchInjector::tap(float& nx, float& ny) const {
  if (!releasedEvent || suppressed || movedBeyondReleaseSlop) return false;
  normalize(downX, downY, nx, ny);
  return true;
}

bool TouchInjector::down(float& nx, float& ny) const {
  if (!pressedEvent) return false;
  normalize(downX, downY, nx, ny);
  return true;
}

bool TouchInjector::tapCandidate(const uint32_t now, float& nx, float& ny, unsigned long& heldMs) const {
  if (!pressed || movedBeyondTapSlop || suppressed) return false;
  normalize(downX, downY, nx, ny);
  heldMs = now - downAt;
  return true;
}

bool TouchInjector::heldAt(float& nx, float& ny) const {
  if (!pressed || suppressed) return false;
  normalize(curX, curY, nx, ny);
  return true;
}

bool TouchInjector::longPress(float& nx, float& ny) const {
  if (!longPressEvent) return false;
  normalize(downX, downY, nx, ny);
  return true;
}

bool TouchInjector::swipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  if (!releasedEvent || suppressed) return false;
  if (latchedHeldMs > SWIPE_MAX_MS) return false;
  if (absInt(curX - downX) < SWIPE_MIN_PX && absInt(curY - downY) < SWIPE_MIN_PX) return false;
  normalize(downX, downY, nxStart, nyStart);
  normalize(curX, curY, nxEnd, nyEnd);
  return true;
}

bool TouchInjector::activity() const {
  const bool screen = pressedEvent || releasedEvent;
  const bool home = homePressEvent || homeTapEvent || homeLongEvent;
  return screen || (!pressed && home);
}

// --- HostPresence ------------------------------------------------------------

void HostPresence::update(const uint32_t now, const bool plugged) {
  if (plugged) {
    isPresent = true;
    absentReadings = 0;
    return;
  }
  if (absentReadings == 0) absentSince = now;
  if (absentReadings < 255) ++absentReadings;
  if (isPresent && absentReadings >= MIN_ABSENT_READINGS && now - absentSince >= absentMs) isPresent = false;
}

// --- InputWaiter ---------------------------------------------------------------

void InputWaiter::start(const uint32_t now, const uint32_t holdMs, const uint32_t settle, const bool keyEdge) {
  deadline = now + holdMs + MARGIN_MS;
  settleMs = settle;
  needsKeyEdge = keyEdge;
  settling = false;
  isLate = false;
  latePasses = 0;
}

InputWaiter::Verdict InputWaiter::poll(const uint32_t now, const bool injectionDone, const bool pressDelivered) {
  if (!injectionDone) {
    if (static_cast<int32_t>(now - deadline) < 0) return Verdict::Wait;
    if (!isLate) {
      isLate = true;
      lateSince = now;
    }
    if (latePasses < 0xFFFF) ++latePasses;
    return (latePasses >= STUCK_PASSES && now - lateSince >= STUCK_MS) ? Verdict::ErrTimeout : Verdict::Wait;
  }
  if (needsKeyEdge && !pressDelivered) return Verdict::ErrNoEdge;
  if (settleMs > 0) {
    if (!settling) {
      settling = true;
      settleUntil = now + settleMs;
      return Verdict::Wait;
    }
    if (static_cast<int32_t>(now - settleUntil) < 0) return Verdict::Wait;
  }
  return Verdict::Ok;
}

// --- Orientation -------------------------------------------------------------

void logicalToPanel(const int orientation, const int x, const int y, const int panelW, const int panelH, int& px,
                    int& py) {
  switch (orientation) {
    case 0:  // Portrait: 90 degrees clockwise onto the panel
      px = y;
      py = panelH - 1 - x;
      break;
    case 1:  // LandscapeClockwise: 180 degrees
      px = panelW - 1 - x;
      py = panelH - 1 - y;
      break;
    case 2:  // PortraitInverted: 90 degrees counter-clockwise
      px = panelW - 1 - y;
      py = x;
      break;
    default:  // LandscapeCounterClockwise: native panel frame
      px = x;
      py = y;
      break;
  }
}

}  // namespace bench
