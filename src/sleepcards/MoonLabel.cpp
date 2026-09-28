#include "MoonLabel.h"

#include <cmath>

namespace sleepcards::moonlabel {

int displayPhase(const int phase, const bool waxing, const bool fullMoonToday) {
  if (phase != 4 || fullMoonToday) return phase;
  return waxing ? 3 : 5;
}

int illuminationPercent(const double illum, const bool fullMoonToday) {
  if (!(illum > 0.0)) return 0;
  int pct = static_cast<int>(std::lround(illum * 100.0));
  if (pct < 1) pct = 1;
  if (pct > 100) pct = 100;
  if (pct == 100 && !fullMoonToday) pct = 99;
  return pct;
}

}  // namespace sleepcards::moonlabel
