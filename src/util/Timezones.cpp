#include "Timezones.h"

#include <HalClock.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"

namespace timezones {

uint8_t activeIndex() {
  const TimezoneInfo* entries = table();
  const size_t entryCount = count();
  if (SETTINGS.clockTimezone < entryCount) return SETTINGS.clockTimezone;
  // Legacy migration: the retired quarter-hour setting was a FIXED offset,
  // and users in DST regions set their current wall offset (the daylight one,
  // half the year), so it must map to a fixed "UTC±HH:MM" entry — matching a
  // region by standard offset could land a summer-configured device one zone
  // over and shift the clock. A real zone is chosen in the picker.
  if (SETTINGS.clockUtcOffsetQ <= 104 && SETTINGS.clockUtcOffsetQ != 48) {
    const int legacyQ = static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48;
    for (size_t i = 0; i < entryCount; i++) {
      if (entries[i].stdOffsetQ == legacyQ && strncmp(entries[i].name, "UTC", 3) == 0) return static_cast<uint8_t>(i);
    }
    // No fixed entry for this offset (only possible for future named-only
    // appends): first match keeps the clock closest to the old value.
    for (size_t i = 0; i < entryCount; i++) {
      if (entries[i].stdOffsetQ == legacyQ) return static_cast<uint8_t>(i);
    }
  }
  return utcIndex();
}

void applyToClock() {
  const TimezoneInfo& tz = table()[activeIndex()];
  if (SETTINGS.clockDst == CrossPointSettings::CLOCK_DST_AUTO) {
    halClock.setTimezone(tz.posixTz);
    return;
  }
  // Forced DST: replace the zone's rule with a fixed offset — the standard
  // offset, plus one hour when forced on. POSIX offset sign is inverted.
  const int q = tz.stdOffsetQ + (SETTINGS.clockDst == CrossPointSettings::CLOCK_DST_ON ? 4 : 0);
  const int absQ = q < 0 ? -q : q;
  char fixed[16];
  snprintf(fixed, sizeof(fixed), "UTC%c%d:%02d", q < 0 ? '+' : '-', absQ / 4, (absQ % 4) * 15);
  halClock.setTimezone(fixed);
}

clockzone::Decision zoneForFix(const geolocate::Fix& fix) {
  clockzone::Settings settings;
  settings.enabled = SETTINGS.clockZoneFromLocation != 0;
  settings.dstAuto = SETTINGS.clockDst == CrossPointSettings::CLOCK_DST_AUTO;
  settings.current = activeIndex();
  return clockzone::decide(fix, settings);
}

bool applyZoneForFix(const clockzone::Decision& decision) {
  if (decision.verdict != clockzone::Verdict::Set || decision.entry < 0 ||
      static_cast<size_t>(decision.entry) >= count()) {
    return false;
  }
  SETTINGS.clockTimezone = static_cast<uint8_t>(decision.entry);
  applyToClock();
  return true;
}

}  // namespace timezones
