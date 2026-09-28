#pragma once

// How the cards name the moon's phase and its illumination, so a card never says "Full moon"
// next to "Full moon tomorrow".
namespace sleepcards::moonlabel {

// Astro's phase (0..7, eighths of the cycle: 0 new, 4 full) names a band about 3.7 days wide.
// "Full moon" is kept to the local date of a full moon; the rest of that band is named for the
// side it is on (3 waxing gibbous, 5 waning gibbous). Other phases pass through.
int displayPhase(int phase, bool waxing, bool fullMoonToday);

// The illuminated percentage as the card prints it: rounded, but 100 only on a full moon's
// date (99.6 % the evening before rounds to 99), and at least 1 when any of it is lit.
int illuminationPercent(double illum, bool fullMoonToday);

}  // namespace sleepcards::moonlabel
