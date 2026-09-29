#include "LocationFix.h"

#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "CardText.h"

namespace sleepcards {
namespace {

constexpr const char* SOURCE_WORDS[] = {"typed", "wifi", "ip"};
constexpr size_t SOURCE_COUNT = sizeof(SOURCE_WORDS) / sizeof(SOURCE_WORDS[0]);

bool validDate(const LocationFix& fix) {
  if (fix.year == 0 && fix.month == 0 && fix.day == 0) return true;  // not known
  return fix.year >= 2000 && fix.year <= 2099 && fix.month >= 1 && fix.month <= 12 && fix.day >= 1 &&
         fix.day <= daysInMonth(fix.year, fix.month);
}

// The next word of text into out, then its terminator: one space, or the end of the text for the
// last word. False when the word is empty, does not fit, or is followed by anything else.
bool nextWord(const char*& p, char* out, const size_t cap, const bool last) {
  if (*p == '\0' || *p == ' ') return false;
  size_t n = 0;
  while (*p != '\0' && *p != ' ') {
    if (n + 1 >= cap) return false;
    out[n++] = *p++;
  }
  out[n] = '\0';
  if (last) return *p == '\0';
  if (*p != ' ') return false;
  ++p;
  return true;
}

bool allDigits(const char* s, const size_t minLen, const size_t maxLen) {
  const size_t n = std::strlen(s);
  if (n < minLen || n > maxLen) return false;
  for (size_t i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

bool parseDate(const char* s, LocationFix& fix) {
  if (std::strcmp(s, "-") == 0) {
    fix.year = 0;
    fix.month = 0;
    fix.day = 0;
    return true;
  }
  if (std::strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
  char y[5] = {s[0], s[1], s[2], s[3], '\0'};
  char m[3] = {s[5], s[6], '\0'};
  char d[3] = {s[8], s[9], '\0'};
  if (!allDigits(y, 4, 4) || !allDigits(m, 2, 2) || !allDigits(d, 2, 2)) return false;
  fix.year = static_cast<uint16_t>((y[0] - '0') * 1000 + (y[1] - '0') * 100 + (y[2] - '0') * 10 + (y[3] - '0'));
  fix.month = static_cast<uint8_t>((m[0] - '0') * 10 + (m[1] - '0'));
  fix.day = static_cast<uint8_t>((d[0] - '0') * 10 + (d[1] - '0'));
  return validDate(fix) && fix.year != 0;
}

}  // namespace

bool formatLocationFix(const LocationFix& fix, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  const auto source = static_cast<size_t>(fix.source);
  char location[LOCATION_CAP];
  if (source >= SOURCE_COUNT || fix.accuracyM > MAX_FIX_ACCURACY_M || !validDate(fix) ||
      !normalizeLocation(fix.location, location, sizeof(location)) || location[0] == '\0') {
    return false;
  }
  char date[12] = "-";
  if (fix.year != 0) {
    std::snprintf(date, sizeof(date), "%04u-%02u-%02u", static_cast<unsigned>(fix.year),
                  static_cast<unsigned>(fix.month), static_cast<unsigned>(fix.day));
  }
  const int n = std::snprintf(out, cap, "%s %lu %s %s", SOURCE_WORDS[source], static_cast<unsigned long>(fix.accuracyM),
                              date, location);
  if (n <= 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool parseLocationFix(const char* text, LocationFix& out) {
  if (text == nullptr) return false;
  const char* p = text;
  char word[LOCATION_CAP];
  LocationFix fix;

  if (!nextWord(p, word, sizeof(word), false)) return false;
  size_t source = SOURCE_COUNT;
  for (size_t i = 0; i < SOURCE_COUNT; i++) {
    if (std::strcmp(word, SOURCE_WORDS[i]) == 0) source = i;
  }
  if (source == SOURCE_COUNT) return false;
  fix.source = static_cast<LocationSource>(source);

  if (!nextWord(p, word, sizeof(word), false) || !allDigits(word, 1, 6)) return false;
  uint32_t accuracy = 0;
  for (const char* c = word; *c; ++c) accuracy = accuracy * 10 + static_cast<uint32_t>(*c - '0');
  fix.accuracyM = accuracy;

  if (!nextWord(p, word, sizeof(word), false) || !parseDate(word, fix)) return false;

  if (!nextWord(p, word, sizeof(word), true)) return false;
  // The location must already be in its stored form.
  if (!normalizeLocation(word, fix.location, sizeof(fix.location)) || fix.location[0] == '\0' ||
      std::strcmp(word, fix.location) != 0) {
    return false;
  }
  out = fix;
  return true;
}

bool normalizeLocationFix(const char* text, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return false;
  if (text == nullptr || text[0] == '\0') {
    out[0] = '\0';
    return true;
  }
  LocationFix fix;
  return parseLocationFix(text, fix) && formatLocationFix(fix, out, cap);
}

bool recordForTypedLocation(const char* before, const char* after, const char* record, const uint16_t year,
                            const uint8_t month, const uint8_t day, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (after == nullptr || after[0] == '\0') return true;
  if (before != nullptr && std::strcmp(before, after) == 0) {
    // Confirmed as it was: where it came from has not changed.
    return formatLocationFix(describeLocation(record, after), out, cap);
  }
  LocationFix typed;
  std::snprintf(typed.location, sizeof(typed.location), "%s", after);
  typed.year = year;
  typed.month = year != 0 ? month : 0;
  typed.day = year != 0 ? day : 0;
  return formatLocationFix(typed, out, cap);
}

LocationFix describeLocation(const char* record, const char* location) {
  LocationFix fix;
  if (location == nullptr || location[0] == '\0') return fix;
  LocationFix stored;
  if (record != nullptr && parseLocationFix(record, stored) && std::strcmp(stored.location, location) == 0) {
    return stored;
  }
  std::snprintf(fix.location, sizeof(fix.location), "%s", location);
  return fix;
}

void formatAccuracy(const uint32_t meters, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  char number[16];
  if (meters < 1000) {
    std::snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(meters));
    std::snprintf(out, cap, tr(STR_DISTANCE_M_FORMAT), number);
    return;
  }
  if (meters < 9950) {
    // Tenths of a kilometre, rounded half up.
    const unsigned long tenths = (meters + 50) / 100;
    std::snprintf(number, sizeof(number), "%lu.%lu", tenths / 10, tenths % 10);
  } else {
    std::snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>((meters + 500) / 1000));
  }
  std::snprintf(out, cap, tr(STR_DISTANCE_KM_FORMAT), number);
}

void formatFixLine(const LocationFix& fix, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (fix.location[0] == '\0') return;

  const char* source = tr(STR_LOCATION_TYPED);
  if (fix.source == LocationSource::Wifi) source = tr(STR_LOCATION_FROM_WIFI);
  if (fix.source == LocationSource::Internet) source = tr(STR_LOCATION_FROM_IP);
  const char* separator = tr(STR_LIST_SEPARATOR);

  // An address lookup measures nothing: it reads "city level", never a distance.
  char accuracy[32] = "";
  if (fix.source == LocationSource::Internet) {
    std::snprintf(accuracy, sizeof(accuracy), "%s%s", separator, tr(STR_LOCATION_CITY_LEVEL));
  } else if (fix.source == LocationSource::Wifi && fix.accuracyM > 0) {
    char amount[16];
    formatAccuracy(fix.accuracyM, amount, sizeof(amount));
    char withSign[24];
    std::snprintf(withSign, sizeof(withSign), tr(STR_LOCATION_ACCURACY_FORMAT), amount);
    std::snprintf(accuracy, sizeof(accuracy), "%s%s", separator, withSign);
  }
  char date[32] = "";
  if (fix.year != 0) {
    char monthDay[24];
    std::snprintf(monthDay, sizeof(monthDay), tr(STR_MONTH_DAY_FORMAT), monthShortName(fix.month),
                  static_cast<unsigned>(fix.day));
    std::snprintf(date, sizeof(date), "%s%s", separator, monthDay);
  }
  std::snprintf(out, cap, "%s%s%s", source, accuracy, date);
}

}  // namespace sleepcards
