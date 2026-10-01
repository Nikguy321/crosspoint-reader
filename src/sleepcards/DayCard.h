#pragma once

#include <LegalLight.h>

#include <cstddef>
#include <cstdint>

#include "SleepCard.h"

// DAY: big date + weekday, sunrise/sunset and day length for the saved location, the moon's
// phase drawing + illumination % + next full moon; with Hunting Season on, the legal light
// window (today's, or tomorrow's once today's has ended). "Screen updated" and the battery are in
// the shared footer. Entry point: sleepcards::renderDayCard() (declared in SleepCard.h).
//
// The facts are computed by computeDayFacts(), pure and host-tested; the renderer only lays
// them out. Cost on the device: one astroSunDay (~49 ms at 160 MHz; a second one only in the
// evening of a hunting day, for tomorrow's window), the moon's phase without its age (~6 ms)
// and one next-full-moon search (~20 ms). No moonrise (that is the Sky card's).
namespace sleepcards::daycard {

enum class SunKind : uint8_t {
  NoLocation,  // no saved location: "Set location in Settings"
  Normal,      // sunrise and sunset both in the local day
  RiseOnly,    // the sun rises and does not set before midnight (polar edge)
  SetOnly,     // the sun sets and did not rise after midnight
  UpAllDay,    // midnight sun
  DownAllDay,  // polar night
};

// Which day's legal-light window the card shows (Hunting Season), if any.
enum class LegalDay : uint8_t { None, Today, Tomorrow };

struct DayFacts {
  LocalDate date;        // the local date at the moment of sleep
  int64_t dayStart = 0;  // its local midnight, UNIX s

  // The sun block: today's sun, or tomorrow's when the legal-light window shown is tomorrow's.
  bool sunTomorrow = false;
  SunKind sunKind = SunKind::NoLocation;
  int64_t sunrise = 0;      // rounded to the NEAREST minute; 0 = none in the day
  int64_t sunset = 0;       // idem
  int64_t dayLengthS = -1;  // sunset - sunrise from the unrounded events; -1 unless Normal

  LegalDay legalDay = LegalDay::None;
  almanac::LegalWindow legal;  // rounded INWARD (LegalLight.h); valid when legalDay != None
  almanac::LegalRule legalRule = almanac::LegalRule::ThirtyMinutes;

  double moonIllum = 0;        // illuminated fraction 0..1 at the moment of sleep
  bool moonWaxing = true;      // east of the sun
  int moonPhase = 0;           // 0..7, astroPhaseName order, as named (moonlabel::displayPhase)
  bool fullMoonToday = false;  // a full moon falls on today's local date
  bool moonSouthern = false;   // draw it as seen from the southern hemisphere (saved location south)
  int64_t nextFullMoon = 0;    // first full moon strictly after the moment of sleep
  LocalDate nextFullDate;      // its local date
  int daysToFullMoon = -1;     // local calendar days from today (0 = today)
};

// Everything the card shows. False when there is no trustworthy time (the card then declines).
bool computeDayFacts(const CardContext& ctx, DayFacts& out);

// "9h 55m": a span of seconds rounded to the NEAREST minute (negative = 0), through tr().
void formatDayLength(int64_t seconds, char* out, size_t cap);

}  // namespace sleepcards::daycard
