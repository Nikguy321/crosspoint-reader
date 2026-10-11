#pragma once

#include <cstddef>
#include <cstdint>

#include "util/ClockZone.h"

// Curated IANA-style timezone list for the clock. Each entry carries the POSIX
// TZ rule newlib applies in localtime_r(), so zones with daylight saving get
// the correct wall time year-round from a UTC-keeping RTC — the old biased
// quarter-hour UTC offset could not represent DST at all.
//
// APPEND ONLY: CrossPointSettings::clockTimezone persists an index into this
// table, so reordering or removing entries retargets users' saved zones
// (test/tz_grid pins the existing entries).
struct TimezoneInfo {
  const char* name;     // shown in the picker; city/region names stay untranslated
  const char* posixTz;  // POSIX TZ rule, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
  int16_t stdOffsetQ;   // standard (non-DST) offset in quarter hours, for display
                        // and for migrating the legacy clockUtcOffsetQ setting
  // The IANA zone the entry stands for where the time zone map (lib/TzGrid) covers it, so a
  // location can find it (clockzone::entryForZone); nullptr elsewhere.
  const char* iana = nullptr;
};

namespace timezones {

// The table itself (src/util/TimezoneTable.cpp, host-tested).
const TimezoneInfo* table();
size_t count();

// Index of plain UTC, the default zone.
uint8_t utcIndex();

// Format an entry's standard offset as "UTC+HH:MM" / "UTC-H:MM" / "UTC".
void formatOffset(uint8_t index, char* buf, size_t bufSize);

// SETTINGS.clockTimezone when valid; otherwise the legacy clockUtcOffsetQ
// mapped to the first entry with the same standard offset, falling back to
// UTC. 255 in the setting means "never chosen".
uint8_t activeIndex();

// Push the active zone's TZ rule into HalClock. Call once at boot after
// settings load, and again whenever clockTimezone changes.
void applyToClock();

// Time Zone From Location: what a saved Locate Me / Update Location When Syncing fix does to the
// clock zone under the settings in force (clockzone::decide). Changes nothing.
clockzone::Decision zoneForFix(const geolocate::Fix& fix);

// Sets the entry a Set decision chose (decision.entry) and pushes it to the clock; false (nothing
// changed) for any other verdict. The caller saves the settings, with the location.
bool applyZoneForFix(const clockzone::Decision& decision);

}  // namespace timezones
