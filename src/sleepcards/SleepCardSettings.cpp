#include "SleepCardSettings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sleepcards {
namespace {

void skipSpaces(const char*& p) {
  while (*p == ' ' || *p == '\t') ++p;
}

// One coordinate: [+-]digits[.digits] [degree sign] [hemisphere letter]. Hand-scanned so
// strtod's extras (hex, exponents, "nan", "inf") can never get in.
bool parseCoordinate(const char*& p, const bool latitude, double& out) {
  skipSpaces(p);
  bool negative = false;
  bool signed_ = false;
  if (*p == '+' || *p == '-') {
    negative = *p == '-';
    signed_ = true;
    ++p;
  }
  char number[16];
  size_t n = 0;
  bool digits = false;
  bool dot = false;
  while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot)) {
    if (*p == '.') dot = true;
    if (*p >= '0' && *p <= '9') digits = true;
    if (n + 1 >= sizeof(number)) return false;
    number[n++] = *p++;
  }
  number[n] = '\0';
  if (!digits) return false;
  double value = std::strtod(number, nullptr);

  skipSpaces(p);
  if (static_cast<unsigned char>(p[0]) == 0xC2 && static_cast<unsigned char>(p[1]) == 0xB0) {  // U+00B0
    p += 2;
    skipSpaces(p);
  }
  const char c = *p;
  const char positive = latitude ? 'N' : 'E';
  const char negativeLetter = latitude ? 'S' : 'W';
  if (c == positive || c == positive + 32 || c == negativeLetter || c == negativeLetter + 32) {
    if (signed_) return false;
    negative = c == negativeLetter || c == negativeLetter + 32;
    ++p;
  }
  if (negative) value = -value;
  const double limit = latitude ? 90.0 : 180.0;
  if (!(value >= -limit && value <= limit)) return false;
  out = value;
  return true;
}

}  // namespace

uint8_t quoteCategories(const QuoteSource source) {
  switch (source) {
    case QuoteSource::BuiltInAndMine:
      return QUOTES_BUILT_IN | QUOTES_MINE;
    case QuoteSource::MineAndBookmarks:
      return QUOTES_MINE | QUOTES_BOOKMARKS;
    case QuoteSource::BuiltInAndBookmarks:
      return QUOTES_BUILT_IN | QUOTES_BOOKMARKS;
    case QuoteSource::BuiltInOnly:
      return QUOTES_BUILT_IN;
    case QuoteSource::MineOnly:
      return QUOTES_MINE;
    case QuoteSource::BookmarksOnly:
      return QUOTES_BOOKMARKS;
    case QuoteSource::All:
    case QuoteSource::Count:
      break;
  }
  return QUOTES_BUILT_IN | QUOTES_MINE | QUOTES_BOOKMARKS;
}

uint8_t migrateQuoteSource(const uint8_t legacy) {
  switch (legacy) {
    case 0:
      return static_cast<uint8_t>(QuoteSource::MineOnly);
    case 1:
      return static_cast<uint8_t>(QuoteSource::BookmarksOnly);
    default:
      return static_cast<uint8_t>(QuoteSource::All);
  }
}

bool parseLocation(const char* text, double& lat, double& lon) {
  if (text == nullptr) return false;
  const char* p = text;
  double a = 0;
  double b = 0;
  if (!parseCoordinate(p, true, a)) return false;
  skipSpaces(p);
  const bool comma = *p == ',';
  if (comma) ++p;
  // Two numbers need something between them.
  if (!comma && p > text && p[-1] != ' ' && p[-1] != '\t' && !(p[-1] >= 'A' && p[-1] <= 'z')) return false;
  if (!parseCoordinate(p, false, b)) return false;
  skipSpaces(p);
  if (*p != '\0') return false;
  lat = a;
  lon = b;
  return true;
}

bool formatLocation(const double lat, const double lon, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return false;
  const int n = std::snprintf(out, cap, "%.4f,%.4f", lat, lon);
  if (n <= 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool normalizeLocation(const char* text, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return false;
  const char* p = text ? text : "";
  skipSpaces(p);
  if (*p == '\0') {
    out[0] = '\0';
    return true;
  }
  double lat = 0;
  double lon = 0;
  if (!parseLocation(text, lat, lon)) return false;
  return formatLocation(lat, lon, out, cap);
}

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysInMonth(const int year, const int month) {
  static constexpr uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29 : DAYS[month - 1];
}

bool parseMonthDay(const char* text, MonthDay& out) {
  if (text == nullptr) return false;
  const char* p = text;
  skipSpaces(p);
  int values[2] = {0, 0};
  for (int i = 0; i < 2; i++) {
    if (i == 1) {
      skipSpaces(p);
      if (*p == '-' || *p == '/' || *p == '.') ++p;
      skipSpaces(p);
    }
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 2) return false;
      values[i] = values[i] * 10 + (*p++ - '0');
    }
    if (digits == 0) return false;
  }
  skipSpaces(p);
  if (*p != '\0') return false;
  if (values[0] < 1 || values[0] > 12 || values[1] < 1 || values[1] > daysInMonth(2000, values[0])) return false;
  out.month = static_cast<uint8_t>(values[0]);
  out.day = static_cast<uint8_t>(values[1]);
  return true;
}

bool huntingSeasonOn(const SleepCardSettings& settings, const int year, const int month, const int day) {
  switch (settings.huntMode) {
    case HuntMode::On:
      return true;
    case HuntMode::Between:
      break;
    default:
      return false;
  }
  const auto key = [year](const MonthDay md) {
    const int m = md.month < 1 ? 1 : (md.month > 12 ? 12 : md.month);
    const int last = daysInMonth(year, m);
    const int d = md.day < 1 ? 1 : (md.day > last ? last : md.day);
    return m * 100 + d;
  };
  const int today = month * 100 + day;
  const int start = key(settings.huntStart);
  const int end = key(settings.huntEnd);
  if (start <= end) return today >= start && today <= end;
  return today >= start || today <= end;  // across New Year
}

}  // namespace sleepcards
