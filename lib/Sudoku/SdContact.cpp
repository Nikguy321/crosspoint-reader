#include "SdContact.h"

namespace sd {

bool canHold(const Target& t) {
  return t.kind == TargetKind::Digit || (t.kind == TargetKind::Tool && t.index == static_cast<int8_t>(Tool::Erase));
}

ContactEvent trackContact(Contact& c, const bool banner, const bool held, const int x, const int y, const bool released,
                          const uint32_t nowMs) {
  ContactEvent ev;
  if (held && !released) {
    const Target here = targetAt(banner, x, y);
    if (!c.active) {
      c = Contact{};
      c.active = true;
      c.start = c.end = here;
      c.downMs = c.endSinceMs = nowMs;
      ev.kind = ContactEvent::Kind::Began;
      ev.target = here;
      return ev;
    }
    if (here != c.end) {
      if (c.changes < 255) c.changes++;
      c.end = here;
      c.endSinceMs = nowMs;
    }
    if (!c.fired && canHold(c.start) && c.end == c.start && nowMs - c.downMs >= HOLD_MS) {
      c.fired = true;
      ev.kind = ContactEvent::Kind::Hold;
      ev.target = c.start;
    }
    return ev;
  }
  if (!c.active) return ev;
  c.active = false;
  ev.ended = true;
  if (c.fired || c.start.kind == TargetKind::None) return ev;
  Target end = c.end;
  // One step off the start just before the end is the lift's drift, not the player.
  if (end != c.start && c.changes == 1 && nowMs - c.endSinceMs < SETTLE_MS) end = c.start;
  if (end == c.start) {
    ev.kind = ContactEvent::Kind::Tap;
    ev.target = c.start;
  }
  return ev;
}

}  // namespace sd
