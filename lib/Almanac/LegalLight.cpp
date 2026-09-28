#include "LegalLight.h"

namespace almanac {

int64_t floorDiv(const int64_t a, const int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

int64_t roundUpToMinute(const int64_t t) { return floorDiv(t + 59, 60) * 60; }
int64_t roundDownToMinute(const int64_t t) { return floorDiv(t, 60) * 60; }
int64_t roundToNearestMinute(const int64_t t) { return floorDiv(t + 30, 60) * 60; }

LegalWindow legalWindow(const AstroSunDay& sun, const int64_t t0, const double lat, const double lon,
                        const LegalRule rule) {
  int64_t first = 0;
  int64_t last = 0;
  astroLegalLight(&sun, static_cast<int>(rule), &first, &last);

  LegalWindow w;
  w.first = first ? roundUpToMinute(first + MODEL_MARGIN_S) : 0;
  w.last = last ? roundDownToMinute(last - MODEL_MARGIN_S) : 0;
  if (first && last) {
    w.kind = LegalWindow::Kind::Window;
  } else if (first) {
    w.kind = LegalWindow::Kind::FromOnly;
  } else if (last) {
    w.kind = LegalWindow::Kind::UntilOnly;
  } else {
    // No bound at all: the sun is either above the rule's threshold all day or below it all day.
    double alt = 0;
    double az = 0;
    astroSunPos(sun.noon ? sun.noon : t0 + 43200, lat, lon, &alt, &az);
    const double threshold = rule == LegalRule::CivilTwilight ? -6.0 : -0.8333;
    w.kind = alt > threshold ? LegalWindow::Kind::AllDay : LegalWindow::Kind::None;
  }
  return w;
}

}  // namespace almanac
