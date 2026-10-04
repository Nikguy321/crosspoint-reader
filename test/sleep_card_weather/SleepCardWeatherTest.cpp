// Weather card: what it shows from a cache record (computeWeatherFacts, pure) - the daily labels by
// each day's middle across a DST change and with the reader's zone ahead of or behind the place's,
// "Now" from the current conditions only within the hour, past hours dropped, alerts that have
// ended hidden, every reason it declines - the unit conversions, and Shuffle's header check on the
// preview fixtures. Public places only (Seattle, Denver, New York city centres).
#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

#include "CardPreview.h"
#include "src/network/WeatherData.h"
#include "src/sleepcards/CardTime.h"
#include "src/sleepcards/ShuffleCard.h"
#include "src/sleepcards/WeatherCache.h"
#include "src/sleepcards/WeatherCard.h"

using namespace sleepcards;
using weathercard::Decline;
using weathercard::WeatherFacts;

namespace {

constexpr const char* TZ_PACIFIC = "PST8PDT,M3.2.0,M11.1.0";
constexpr const char* TZ_EASTERN = "EST5EDT,M3.2.0,M11.1.0";
constexpr double SEA_LAT = 47.61, SEA_LON = -122.33;

void useZone(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

// The reader at a local wall time in its zone (TZ), Weather on, located at lat/lon.
CardContext readerAt(const char* tz, const int y, const int mo, const int d, const int h, const int mi,
                     const double lat = SEA_LAT, const double lon = SEA_LON) {
  CardContext ctx = preview::sampleContext();
  useZone(tz);
  ctx.utcNow = localDayStart(y, mo, d, ctx.utcOffsetAt) + h * 3600 + mi * 60;
  ctx.utcOffsetS = libcUtcOffset(ctx.utcNow);
  ctx.localNow = localDateOf(ctx.utcNow, ctx.utcOffsetAt);
  ctx.location.lat = lat;
  ctx.location.lon = lon;
  ctx.settings.weatherOn = true;
  return ctx;
}

int64_t utcOf(const int y, const int mo, const int d, const int h, const int offsetS) {
  return daysFromCivil(y, mo, d) * 86400 + h * 3600 - offsetS;
}

// A forecast as Open-Meteo sends one: daily times are the PLACE's midnights, all computed with the
// one offset it had at the fetch (placeOffsetS), and 48 hours from the fetch's hour.
std::unique_ptr<weather::Record> recordAt(const int64_t fetchUtc, const int placeOffsetS, const int y, const int mo,
                                          const int d, const double lat = SEA_LAT, const double lon = SEA_LON) {
  auto r = std::make_unique<weather::Record>();
  r->fetchUtc = fetchUtc;
  r->clockTrusted = true;
  r->lat = lat;
  r->lon = lon;
  weather::Forecast& f = r->forecast;
  f.utcOffsetS = placeOffsetS;
  f.current.valid = true;
  f.current.tempC10 = 123;
  f.current.code = 3;
  f.current.windDir = 200;
  f.current.windMs10 = 50;
  const int64_t firstHour = fetchUtc - fetchUtc % 3600;
  f.hourCount = weather::MAX_HOURS;
  for (int i = 0; i < weather::MAX_HOURS; i++) {
    f.hours[i].t = firstHour + i * 3600;
    f.hours[i].tempC10 = static_cast<int16_t>(100 + i);
    f.hours[i].windMs10 = 30;
    f.hours[i].code = 61;
  }
  f.dayCount = weather::MAX_DAYS;
  for (int i = 0; i < weather::MAX_DAYS; i++) {
    f.days[i].t = utcOf(y, mo, d, 0, placeOffsetS) + i * 86400;
    f.days[i].code = 1;
    f.days[i].maxC10 = static_cast<int16_t>(150 + i);
    f.days[i].minC10 = 50;
  }
  r->alerts.status = weather::AlertsStatus::None;
  r->alerts.asOf = fetchUtc;
  return r;
}

Decline facts(const CardContext& ctx, const weather::Record& r, WeatherFacts& f) {
  return weathercard::computeWeatherFacts(ctx, r, f);
}

}  // namespace

// ---- daily labels -------------------------------------------------------------------------------

TEST(WeatherCard, DaysAreLabelledByTheirMiddleAcrossTheDstChange) {
  // Fetched Oct 31 at noon PDT. Open-Meteo stamps every day with that day's offset (-7 h), so
  // "Nov 2" arrives as 2026-11-02 07:00Z = Nov 1 23:00 PST: by its start it is the wrong day.
  const auto r = recordAt(utcOf(2026, 10, 31, 12, -25200), -25200, 2026, 10, 31);
  const CardContext ctx = readerAt(TZ_PACIFIC, 2026, 10, 31, 13, 0);
  WeatherFacts f;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  EXPECT_EQ(f.firstDay, 0);
  const int expectDay[] = {31, 1, 2, 3, 4, 5, 6};
  for (int i = 0; i < weather::MAX_DAYS; i++) {
    EXPECT_EQ(f.dayDate[i].day, expectDay[i]) << i;
    EXPECT_EQ(localDateOf(r->forecast.days[i].t, ctx.utcOffsetAt).day, i < 2 ? expectDay[i] : expectDay[i] - 1)
        << "by its start, day " << i;
  }
  EXPECT_FALSE(f.offsetMismatch);  // PDT both, at the fetch
  // Drawn on Nov 3 (PST now): today's row is Nov 3, the 4th entry.
  const CardContext later = readerAt(TZ_PACIFIC, 2026, 11, 3, 8, 0);
  const auto r2 = recordAt(later.utcNow - 3600, -25200, 2026, 10, 31);
  ASSERT_EQ(facts(later, *r2, f), Decline::None);
  EXPECT_EQ(f.firstDay, 3);
  EXPECT_EQ(f.dayDate[f.firstDay].day, 3);
  EXPECT_EQ(f.dayCount, 4);
}

TEST(WeatherCard, ReaderBehindThePlacesZoneStillGetsTheRightDay) {
  // The place keeps Mountain time (-6 h in summer) but the reader is set to Pacific (-7 h): each
  // place midnight is 23:00 the day before on the reader. By the middle, the dates hold.
  const auto r = recordAt(utcOf(2026, 7, 10, 9, -21600), -21600, 2026, 7, 10, 39.74, -104.99);
  const CardContext ctx = readerAt(TZ_PACIFIC, 2026, 7, 10, 9, 0, 39.74, -104.99);
  WeatherFacts f;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  for (int i = 0; i < weather::MAX_DAYS; i++) EXPECT_EQ(f.dayDate[i].day, 10 + i) << i;
  EXPECT_EQ(localDateOf(r->forecast.days[0].t, ctx.utcOffsetAt).day, 9);  // by its start: wrong
  EXPECT_TRUE(f.offsetMismatch);  // "Local time there is UTC-6 - check the clock setting"
}

TEST(WeatherCard, ReaderAheadOfThePlacesZoneStillGetsTheRightDay) {
  // The place on Pacific time (-7 h in summer), the reader set to Eastern (-4 h).
  const auto r = recordAt(utcOf(2026, 7, 10, 9, -25200), -25200, 2026, 7, 10);
  const CardContext ctx = readerAt(TZ_EASTERN, 2026, 7, 10, 14, 0);
  WeatherFacts f;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  for (int i = 0; i < weather::MAX_DAYS; i++) EXPECT_EQ(f.dayDate[i].day, 10 + i) << i;
  EXPECT_TRUE(f.offsetMismatch);
}

// ---- now, hours, alerts -------------------------------------------------------------------------

TEST(WeatherCard, NowIsTheCurrentConditionsOnlyWithinTheHour) {
  const CardContext ctx = readerAt(TZ_PACIFIC, 2026, 11, 2, 20, 40);
  WeatherFacts f;
  auto r = recordAt(ctx.utcNow - 50 * 60, -28800, 2026, 11, 2);
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  EXPECT_TRUE(f.haveNow);
  EXPECT_TRUE(f.nowIsCurrent);
  EXPECT_EQ(f.tempC10, 123);
  EXPECT_EQ(f.windDir, 200);
  // Two hours on: the forecast hour covering 20:40, labelled as such, with no direction claimed.
  r = recordAt(ctx.utcNow - 2 * 3600, -28800, 2026, 11, 2);
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  EXPECT_TRUE(f.haveNow);
  EXPECT_FALSE(f.nowIsCurrent);
  EXPECT_EQ(f.nowHourUtc, ctx.utcNow - 40 * 60);
  EXPECT_EQ(f.tempC10, r->forecast.hours[f.firstHour].tempC10);
  EXPECT_EQ(f.windDir, weather::NO_VALUE);
}

TEST(WeatherCard, PastHoursAreDroppedAndTheStripStartsAtTheDrawTime) {
  const CardContext ctx = readerAt(TZ_PACIFIC, 2026, 11, 2, 20, 40);
  const auto r = recordAt(ctx.utcNow - 5 * 3600 - 600, -28800, 2026, 11, 2);
  WeatherFacts f;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  const weather::Hour& first = r->forecast.hours[f.firstHour];
  EXPECT_LE(first.t, ctx.utcNow);
  EXPECT_GT(first.t + 3600, ctx.utcNow);
  EXPECT_EQ(f.hourCount, weathercard::STRIP_HOURS);
  // Near the end of the series: fewer hours, then none (the days still show).
  const auto old = recordAt(ctx.utcNow - 46 * 3600, -28800, 2026, 11, 1);
  EXPECT_EQ(facts(ctx, *old, f), Decline::Stale);
  // A shorter series (12 hours): near its end fewer hours, then none (the days still show).
  CardContext late = ctx;
  auto r2 = recordAt(ctx.utcNow - 600, -28800, 2026, 11, 2);
  r2->forecast.hourCount = 12;
  late.utcNow = r2->forecast.hours[11].t + 1800;
  ASSERT_EQ(facts(late, *r2, f), Decline::None);
  EXPECT_EQ(f.hourCount, 1);
  late.utcNow = r2->forecast.hours[11].t + 3600;
  ASSERT_EQ(facts(late, *r2, f), Decline::None);
  EXPECT_EQ(f.hourCount, 0);
  EXPECT_FALSE(f.haveNow);
  EXPECT_GT(f.dayCount, 0);
}

TEST(WeatherCard, AlertsThatHaveEndedAreHidden) {
  const CardContext ctx = readerAt(TZ_PACIFIC, 2026, 11, 2, 20, 40);
  auto r = recordAt(ctx.utcNow - 600, -28800, 2026, 11, 2);
  weather::Alerts& a = r->alerts;
  a.status = weather::AlertsStatus::Listed;
  a.total = 3;
  a.count = 3;
  a.list[0].ends = ctx.utcNow - 60;  // over
  a.list[1].ends = 0;
  a.list[1].expires = ctx.utcNow + 3600;  // no `ends`: until its message expires
  a.list[2].ends = ctx.utcNow + 7200;
  a.list[2].expires = ctx.utcNow - 7200;  // the message expired; the event goes on
  WeatherFacts f;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  ASSERT_EQ(f.alertCount, 2);
  EXPECT_EQ(f.alertIndex[0], 1);
  EXPECT_EQ(f.alertIndex[1], 2);
}

TEST(WeatherCard, AlertsNotKeptMayStillBeInForce) {
  // Five in force at the check; the three most severe kept, and all three over by the draw.
  const CardContext ctx = readerAt(TZ_PACIFIC, 2026, 11, 3, 8, 0);
  auto r = recordAt(ctx.utcNow - 12 * 3600, -28800, 2026, 11, 2);
  weather::Alerts& a = r->alerts;
  a.status = weather::AlertsStatus::Listed;
  a.total = 5;
  a.count = 3;
  for (int i = 0; i < 3; i++) a.list[i].ends = ctx.utcNow - 7200;
  WeatherFacts f;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  EXPECT_EQ(f.alertCount, 0);
  EXPECT_EQ(weathercard::alertsInForce(a, f), 2);  // never "have ended"

  // All listed: the one over is not counted as "more".
  a.total = 3;
  a.list[1].ends = ctx.utcNow + 3600;
  a.list[2].ends = ctx.utcNow + 7200;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  EXPECT_EQ(f.alertCount, 2);
  EXPECT_EQ(weathercard::alertsInForce(a, f), 2);  // the two shown, no "+1 more"
  // All listed and all over: none.
  for (int i = 0; i < 3; i++) a.list[i].ends = ctx.utcNow - 60;
  ASSERT_EQ(facts(ctx, *r, f), Decline::None);
  EXPECT_EQ(weathercard::alertsInForce(a, f), 0);
}

TEST(WeatherCard, AnAlertsTimeLineIsNeverAnImpossibleRange) {
  CardContext ctx = readerAt(TZ_PACIFIC, 2026, 11, 2, 20, 0);  // Mon
  ctx.clock12h = false;
  weather::Alert al;
  char line[96];
  // A Watch from Wed 10:00 whose message expires Tue 04:00, with no `ends`: only its start.
  al.onset = ctx.utcNow + 38 * 3600;
  al.expires = ctx.utcNow + 8 * 3600;
  weathercard::formatAlertTime(ctx, al, line, sizeof(line));
  EXPECT_STREQ(line, "from 10:00 Wed");
  // With a real end after the onset: the range.
  al.ends = ctx.utcNow + 68 * 3600;
  weathercard::formatAlertTime(ctx, al, line, sizeof(line));
  EXPECT_STREQ(line, "from 10:00 Wed until 16:00 Thu");
  // Begun: its end only; neither given: nothing.
  al.onset = ctx.utcNow - 3600;
  weathercard::formatAlertTime(ctx, al, line, sizeof(line));
  EXPECT_STREQ(line, "until 16:00 Thu");
  al.ends = 0;
  al.expires = 0;
  weathercard::formatAlertTime(ctx, al, line, sizeof(line));
  EXPECT_STREQ(line, "");
}

// ---- declining ----------------------------------------------------------------------------------

TEST(WeatherCard, DeclinesWhenItHasNothingTrueToShow) {
  const CardContext ctx = readerAt(TZ_PACIFIC, 2026, 11, 2, 20, 40);
  WeatherFacts f;
  auto fresh = recordAt(ctx.utcNow - 600, -28800, 2026, 11, 2);
  ASSERT_EQ(facts(ctx, *fresh, f), Decline::None);

  CardContext c = ctx;
  c.timeValid = false;
  EXPECT_EQ(facts(c, *fresh, f), Decline::NoClock);
  c = ctx;
  c.settings.weatherOn = false;
  EXPECT_EQ(facts(c, *fresh, f), Decline::Off);
  c = ctx;
  c.location.valid = false;
  EXPECT_EQ(facts(c, *fresh, f), Decline::NoLocation);
  EXPECT_EQ(facts(ctx, weather::Record{}, f), Decline::NoCache);
  // Over 36 h old, or stamped in the future (a clock that was wrong).
  EXPECT_EQ(facts(ctx, *recordAt(ctx.utcNow - 37 * 3600, -28800, 2026, 11, 1), f), Decline::Stale);
  EXPECT_EQ(facts(ctx, *recordAt(ctx.utcNow - 35 * 3600, -28800, 2026, 11, 1), f), Decline::None);
  EXPECT_EQ(facts(ctx, *recordAt(ctx.utcNow + 3600, -28800, 2026, 11, 2), f), Decline::FromTheFuture);
  // A place more than 5 km from the saved location.
  c = ctx;
  c.location.lat = SEA_LAT + 0.06;
  EXPECT_EQ(facts(c, *fresh, f), Decline::Moved);
  c.location.lat = SEA_LAT + 0.04;
  EXPECT_EQ(facts(c, *fresh, f), Decline::None);
  // Every day already over.
  auto over = recordAt(ctx.utcNow - 600, -28800, 2026, 10, 20);
  EXPECT_EQ(facts(ctx, *over, f), Decline::Ended);
}

// ---- units --------------------------------------------------------------------------------------

TEST(WeatherUnits, TemperatureRoundsOnceAndNeverShowsMinusZero) {
  using weathercard::temperature;
  EXPECT_EQ(temperature(104, WeatherUnits::Metric), 10);
  EXPECT_EQ(temperature(105, WeatherUnits::Metric), 11);
  EXPECT_EQ(temperature(-4, WeatherUnits::Metric), 0);
  EXPECT_EQ(temperature(-5, WeatherUnits::Metric), -1);
  EXPECT_EQ(temperature(100, WeatherUnits::Us), 50);
  EXPECT_EQ(temperature(-400, WeatherUnits::Us), -40);
  EXPECT_EQ(temperature(370, WeatherUnits::Us), 99);  // 98.6
  EXPECT_EQ(temperature(-178, WeatherUnits::Us), 0);  // -0.04 F
  char out[16];
  weathercard::formatTemperature(-4, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "0°");
  weathercard::formatTemperature(111, WeatherUnits::Us, out, sizeof(out));
  EXPECT_STREQ(out, "52°");
  weathercard::formatTemperature(weather::NO_VALUE, WeatherUnits::Us, out, sizeof(out));
  EXPECT_STREQ(out, "--");
}

TEST(WeatherUnits, WindSaysWhereItComesFrom) {
  char out[64];
  weathercard::formatWind(315, 39, 83, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "from NW 14 km/h, gusts 30");
  weathercard::formatWind(315, 39, 83, WeatherUnits::Us, out, sizeof(out));
  EXPECT_STREQ(out, "from NW 9 mph, gusts 19");
  weathercard::formatWind(5, 23, 23, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "from N 8 km/h");  // gusts no stronger: left out
  weathercard::formatWind(5, 23, weather::NO_VALUE, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "from N 8 km/h");
  weathercard::formatWind(90, 1, 2, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "Calm");
  weathercard::formatWind(weather::NO_VALUE, 23, weather::NO_VALUE, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "Wind 8 km/h");
  weathercard::formatWind(90, weather::NO_VALUE, 50, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "--");
}

TEST(WeatherUnits, PrecipitationAndPressure) {
  char out[32];
  weathercard::formatPrecipitation(27, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "2.7 mm");
  weathercard::formatPrecipitation(27, WeatherUnits::Us, out, sizeof(out));
  EXPECT_STREQ(out, "0.11 in");
  weathercard::formatPrecipitation(0, WeatherUnits::Us, out, sizeof(out));
  EXPECT_STREQ(out, "0.00 in");
  weathercard::formatPressure(10179, WeatherUnits::Metric, out, sizeof(out));
  EXPECT_STREQ(out, "1018 hPa");
  weathercard::formatPressure(10179, WeatherUnits::Us, out, sizeof(out));
  EXPECT_STREQ(out, "30.06 inHg");
  weathercard::formatPressure(weather::NO_VALUE, WeatherUnits::Us, out, sizeof(out));
  EXPECT_STREQ(out, "--");
}

TEST(WeatherUnits, TheSharedWmoWords) {
  char out[32];
  const struct {
    int16_t code;
    const char* words;
    weathercard::Icon icon;
  } table[] = {
      {0, "Clear", weathercard::Icon::Clear},
      {1, "Mostly clear", weathercard::Icon::Clear},
      {2, "Partly cloudy", weathercard::Icon::PartlyCloudy},
      {3, "Overcast", weathercard::Icon::Overcast},
      {45, "Fog", weathercard::Icon::Fog},
      {48, "Rime fog", weathercard::Icon::Fog},
      {53, "Drizzle", weathercard::Icon::Drizzle},
      {56, "Lt frz drizzle", weathercard::Icon::Freezing},
      {61, "Lt rain", weathercard::Icon::Rain},
      {67, "Frz rain", weathercard::Icon::Freezing},
      {73, "Snow", weathercard::Icon::Snow},
      {77, "Snow grains", weathercard::Icon::Snow},
      {81, "Showers", weathercard::Icon::Showers},
      {86, "Hvy snow shwrs", weathercard::Icon::Snow},
      {95, "T-storm", weathercard::Icon::Thunder},
      {99, "T-storm+hvy hail", weathercard::Icon::Thunder},
      {42, "Code 42", weathercard::Icon::Unknown},
  };
  for (const auto& row : table) {
    weathercard::conditionName(row.code, out, sizeof(out));
    EXPECT_STREQ(out, row.words) << row.code;
    EXPECT_EQ(weathercard::iconFor(row.code), row.icon) << row.code;
  }
  weathercard::conditionName(weather::NO_VALUE, out, sizeof(out));
  EXPECT_STREQ(out, "--");
}

// ---- Shuffle ------------------------------------------------------------------------------------

TEST(WeatherCard, ShuffleDealsItOnlyWithAUsableCache) {
  preview::hostIo().weatherFixture = "weather.dat";
  CardContext ctx = preview::sampleContext();
  ctx.settings.shuffleMask = cardBit(CardId::Weather) | cardBit(CardId::Day);
  EXPECT_TRUE(weathercard::cacheLooksUsable(ctx));
  EXPECT_TRUE(shuffle::shuffleUsableMask(ctx, ctx.settings.shuffleMask) & cardBit(CardId::Weather));
  EXPECT_TRUE(shuffle::pickableMask() & cardBit(CardId::Weather));
  EXPECT_FALSE(shuffle::pickableMask() & cardBit(CardId::Shuffle));

  CardContext off = ctx;
  off.settings.weatherOn = false;
  EXPECT_FALSE(weathercard::cacheLooksUsable(off));
  CardContext moved = ctx;
  moved.location.lat += 0.1;
  EXPECT_FALSE(weathercard::cacheLooksUsable(moved));
  CardContext later = ctx;
  later.utcNow += 40 * 3600;  // over 36 h after the fetch
  EXPECT_FALSE(weathercard::cacheLooksUsable(later));
  CardContext muchLater = ctx;
  muchLater.utcNow += 8 * 86400;
  EXPECT_FALSE(weathercard::cacheLooksUsable(muchLater));
  preview::hostIo().weatherFixture = "";
  EXPECT_FALSE(weathercard::cacheLooksUsable(ctx));
  EXPECT_FALSE(shuffle::shuffleUsableMask(ctx, ctx.settings.shuffleMask) & cardBit(CardId::Weather));
  preview::hostIo().weatherFixture = "weather.dat";
}

// An old shuffle.dat (written before Weather existed) still reads, and one naming Weather does too.
TEST(WeatherCard, ShuffleStateWithWeather) {
  shuffle::ShuffleState st;
  EXPECT_TRUE(shuffle::parseState("S1 7 00fe\n", 10, st));
  EXPECT_TRUE(shuffle::parseState("S1 9 0204\n", 10, st));
  EXPECT_EQ(st.last, CardId::Weather);
  EXPECT_FALSE(shuffle::parseState("S1 8 0004\n", 10, st));  // Shuffle itself is never dealt
}

// Every preview fixture is a well-formed cache.
TEST(WeatherCard, PreviewFixturesDecode) {
  for (const char* name : {"weather.dat", "weather_alert.dat", "weather_usonly.dat", "weather_notchecked.dat",
                           "weather_toomany.dat", "weather_cooling.dat", "weather_lapsed.dat"}) {
    preview::hostIo().weatherFixture = name;
    char buf[weather::CACHE_CAP];
    const int32_t got = preview::hostIo().readFileAt(weather::CACHE_PATH, 0, buf, sizeof(buf));
    ASSERT_GT(got, 0) << name;
    auto r = std::make_unique<weather::Record>();
    EXPECT_TRUE(weather::decodeCache(buf, static_cast<size_t>(got), *r)) << name;
  }
  preview::hostIo().weatherFixture = "weather.dat";
}
