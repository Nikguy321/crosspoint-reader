#include "BookSyncPatience.h"

void BookSyncPatience::arm(const uint32_t window, const uint32_t nowMs) {
  windowMs = window;
  startMs = nowMs;
  rescanAtMs = nowMs;
  isWaiting = false;
}

void BookSyncPatience::disarm() {
  windowMs = 0;
  isWaiting = false;
}

bool BookSyncPatience::expired(const uint32_t nowMs) const { return armed() && nowMs - startMs >= windowMs; }

bool BookSyncPatience::nothingToJoin(const uint32_t nowMs) {
  if (!armed() || expired(nowMs)) {
    isWaiting = false;
    return false;
  }
  isWaiting = true;
  rescanAtMs = nowMs + RESCAN_INTERVAL_MS;
  return true;
}

bool BookSyncPatience::takeRescan(const uint32_t nowMs) {
  // Signed view of the unsigned difference: due once nowMs has reached rescanAtMs.
  if (!isWaiting || static_cast<int32_t>(nowMs - rescanAtMs) < 0) return false;
  isWaiting = false;
  return true;
}

uint32_t BookSyncPatience::secondsLeft(const uint32_t nowMs) const {
  if (!armed() || expired(nowMs)) return 0;
  const uint32_t leftMs = windowMs - (nowMs - startMs);
  return (leftMs + 999U) / 1000U;
}
