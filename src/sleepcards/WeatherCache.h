#pragma once

#include <cstddef>
#include <cstdint>

#include "network/WeatherData.h"

// The Weather card's one cached forecast on the SD card: what network/WeatherFetch last parsed,
// in SI units (the card converts when it draws), read by the card through CardIo. A versioned text
// file of at most CACHE_CAP bytes, so the card reads it with one bounded readFileAt:
//
//   W1 <fetch utc> <clock trusted 0|1> <lat> <lon> <last day utc>   the header (Shuffle reads only this)
//   P <place source> <fix date yyyymmdd|-> <location utc offset s>
//   C <temp> <feels> <rh> <hPa> <wind> <gust> <dir> <code>          or "C -" (no reading)
//   H <utc> <temp> <pop> <code> <wind>                              one per hour
//   D <utc> <code> <max> <min> <pop> <precip> <wind max> <gust max> <dir>   one per day
//   N <alerts status> <as-of utc> <total> <recheck failed 0|1>
//   A <severity> <urgency> <message type> <onset> <ends> <expires>  then E <event>, L <headline>
//   end
//
// Values are WeatherData.h's fixed point ("-" = no value); lat/lon is the rounded point the
// forecast was fetched for (2 decimals). Anything else - a stray byte, an unknown line, a count
// over the maximum, a missing "end" - reads as no cache at all.
namespace weather {

constexpr const char* CACHE_PATH = "/.crosspoint/sleepcards/weather.dat";
constexpr const char* CACHE_TMP_PATH = "/.crosspoint/sleepcards/weather.tmp";
constexpr size_t CACHE_CAP = 4096;  // CardIo::writeFile's cap, and one read
constexpr size_t HEADER_CAP = 64;   // the first line, for Shuffle's cheap check

// Where the location the forecast was fetched for came from (sleepcards::LocationSource values).
constexpr uint8_t PLACE_TYPED = 0;
constexpr uint8_t PLACE_WIFI = 1;
constexpr uint8_t PLACE_INTERNET = 2;
constexpr uint8_t PLACE_WIFI_AUTO = 3;

struct Record {
  int64_t fetchUtc = 0;       // when the forecast was fetched (the reader's clock)
  bool clockTrusted = false;  // that clock had been set from the internet
  double lat = 0;             // the point it was fetched for, rounded to 0.01
  double lon = 0;
  uint8_t placeSource = PLACE_TYPED;
  uint32_t placeYmd = 0;  // the day that location was saved, 20260929; 0 = not known
  Forecast forecast;
  Alerts alerts;
};

// The header line alone.
struct Header {
  int64_t fetchUtc = 0;
  double lat = 0;
  double lon = 0;
  int64_t lastDayUtc = 0;  // the last daily entry's time
};

// The file text, or 0 when it does not fit cap (nothing written).
size_t encodeCache(const Record& record, char* out, size_t cap);
// True with record filled from a well-formed file; false (record reset) otherwise.
bool decodeCache(const char* text, size_t len, Record& record);
// The first line of a cache file (len may cover more of it).
bool decodeHeader(const char* text, size_t len, Header& header);

}  // namespace weather
