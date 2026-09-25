#pragma once

#include <cstdint>

// The "patient" half of a sync event's Wi-Fi connect: while armed, a scan (or
// a join attempt) that leaves nothing to join schedules a rescan instead of
// dropping to the network list, until the window runs out. Times are millis()
// values; every comparison is by unsigned difference, so the 49-day wrap is safe.
class BookSyncPatience {
 public:
  static constexpr uint32_t RESCAN_INTERVAL_MS = 5000;

  // windowMs == 0 disarms (the stock network list).
  void arm(uint32_t windowMs, uint32_t nowMs);
  void disarm();
  bool armed() const { return windowMs != 0; }

  // Between scans, holding the countdown screen.
  bool waiting() const { return isWaiting; }

  // A scan or join attempt left nothing to join. True: keep waiting, a rescan is
  // scheduled. False: not armed, or the window ran out (the caller gives up).
  bool nothingToJoin(uint32_t nowMs);

  bool expired(uint32_t nowMs) const;

  // True once when the scheduled rescan is due; the waiting state ends with it.
  bool takeRescan(uint32_t nowMs);

  // Whole seconds left in the window, rounded up (0 once expired or disarmed).
  uint32_t secondsLeft(uint32_t nowMs) const;

 private:
  uint32_t windowMs = 0;
  uint32_t startMs = 0;
  uint32_t rescanAtMs = 0;
  bool isWaiting = false;
};
