#pragma once

// Sudoku touch: one contact at a time, fed a frame at a time from MappedInputManager
// (isScreenTouchHeld + wasScreenTouchReleased). A copy of Crossword's CwContact, generalised so
// that any digit key or Erase can hold (Crossword's own tracker is untouched). Pure, so the host
// tests drive it.
//
// A contact is a tap when it starts and ends on the same target (one key per contact; sliding
// off cancels). The end is the target under the LAST held position (the release frame carries
// none). The panel's centroid drifts 10-20 px as a finger rolls off at lift, so a contact whose
// end left the start target once, less than SETTLE_MS before the contact ended, still taps the
// start target. Holding a digit key or Erase for HOLD_MS fires a Hold at once (the release then
// does nothing). A contact ends on the release edge or on the first frame it is no longer held
// (a second finger silences the single-contact queries but not the release).

#include <cstdint>

#include "SdLayout.h"

namespace sd {

constexpr uint32_t SETTLE_MS = 80;
constexpr uint32_t HOLD_MS = 600;  // KEY_LONG_PRESS_MS

struct Contact {
  bool active = false;
  bool fired = false;  // a Hold acted: the release is not a tap
  Target start;
  Target end;           // under the last held position
  uint8_t changes = 0;  // how often `end` changed (capped)
  uint32_t downMs = 0;
  uint32_t endSinceMs = 0;
};

struct ContactEvent {
  enum class Kind : uint8_t {
    None,
    Began,  // a finger came down (nothing to act on yet)
    Tap,    // the contact ended on its start target
    Hold,   // a digit key or Erase was held HOLD_MS
  };
  Kind kind = Kind::None;
  Target target;
  bool ended = false;  // the contact ended this frame
};

// The targets a held contact fires on: the digit keys and Erase.
bool canHold(const Target& target);

// banner: the completion banner is up (targets as sd::targetAt).
ContactEvent trackContact(Contact& contact, bool banner, bool held, int x, int y, bool released, uint32_t nowMs);

}  // namespace sd
