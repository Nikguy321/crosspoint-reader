#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Which saved Wi-Fi networks to join, in what order, from one scan: WifiSelectionActivity's
// auto-connect and live sleep's StationKeeper both ask this, so a reader that does not see its
// last network joins the next-best saved one in view instead. Pure (test/wifi_join_order).
namespace wifi_join {

// The Wi-Fi list holds at most this many networks (WifiCredentialStore).
constexpr size_t MAX_CANDIDATES = 8;

struct Seen {
  std::string_view ssid;  // "" = a hidden network
  int32_t rssi = 0;
  bool saved = false;  // in the Wi-Fi list
};

// Indices into seen[] (written to out, at most cap), in join order: the last-connected network
// first when it is in view, then the other saved networks in view, strongest first. One entry per
// SSID (its strongest sighting); never a hidden, unsaved or excluded SSID. Returns the count.
inline size_t order(const Seen* seen, const size_t count, const std::string_view lastSsid,
                    const std::string_view* excluded, const size_t excludedCount, uint16_t* out, const size_t cap) {
  if (seen == nullptr || out == nullptr || cap == 0) return 0;
  const auto isExcluded = [&](const std::string_view ssid) {
    for (size_t e = 0; e < excludedCount; e++) {
      if (!excluded[e].empty() && excluded[e] == ssid) return true;
    }
    return false;
  };
  // The last network ranks above any signal; otherwise stronger first, then scan order.
  const auto before = [&](const Seen& a, const Seen& b) {
    const bool aLast = !lastSsid.empty() && a.ssid == lastSsid;
    const bool bLast = !lastSsid.empty() && b.ssid == lastSsid;
    if (aLast != bLast) return aLast;
    return a.rssi > b.rssi;
  };
  size_t n = 0;
  for (size_t i = 0; i < count && i <= UINT16_MAX; i++) {
    const Seen& s = seen[i];
    if (!s.saved || s.ssid.empty() || isExcluded(s.ssid)) continue;
    // Already listed: keep the stronger sighting.
    size_t at = n;
    for (size_t j = 0; j < n; j++) {
      if (seen[out[j]].ssid == s.ssid) {
        at = j;
        break;
      }
    }
    if (at < n) {
      if (s.rssi <= seen[out[at]].rssi) continue;
      for (size_t j = at; j + 1 < n; j++) out[j] = out[j + 1];  // re-placed below
      n--;
    } else if (n == cap && !before(s, seen[out[n - 1]])) {
      continue;
    }
    if (n == cap) n--;  // the weakest makes room
    size_t pos = n;
    while (pos > 0 && before(s, seen[out[pos - 1]])) {
      out[pos] = out[pos - 1];
      pos--;
    }
    out[pos] = static_cast<uint16_t>(i);
    n++;
  }
  return n;
}

// After every saved network in view failed: a last network that was not in view may be a hidden
// one (a hidden network answers only a join that names it), so it is tried blind once, but only
// when the scan saw a hidden network at all.
constexpr bool tryHiddenLast(const bool lastSaved, const bool lastSeen, const bool hiddenSeen) {
  return lastSaved && !lastSeen && hiddenSeen;
}

}  // namespace wifi_join
