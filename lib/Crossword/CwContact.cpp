#include "CwContact.h"

#include "CwKeyboard.h"

namespace cw {

namespace {

bool isDelKey(const Target& t) { return t.kind == TargetKind::Key && t.index == delKey(); }

}  // namespace

ContactEvent trackContact(Contact& c, const ScreenLayout& layout, const bool banner, const bool held, const int x,
                          const int y, const bool released, const uint32_t nowMs) {
  ContactEvent ev;
  if (held && !released) {
    const Target here = targetAt(layout, banner, x, y);
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
    if (!c.fired && isDelKey(c.start) && c.end == c.start && nowMs - c.downMs >= DEL_HOLD_MS) {
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

}  // namespace cw
