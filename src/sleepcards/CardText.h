#pragma once

// Translated calendar words for the cards (tr() keys in english.yaml).
namespace sleepcards {

// month 1..12: "November" / "Nov". "" outside the range.
const char* monthName(int month);
const char* monthShortName(int month);
// weekday 0..6, 0 = Sunday: "Monday" / "Mon". "" outside the range.
const char* weekdayName(int weekday);
const char* weekdayShortName(int weekday);

}  // namespace sleepcards
