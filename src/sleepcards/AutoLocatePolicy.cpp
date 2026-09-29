#include "AutoLocatePolicy.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "CardTime.h"
#include "LocationFix.h"
#include "SleepCardSettings.h"

namespace sleepcards::autolocate {
namespace {

bool sameNetwork(const char* ssid, const char* other) {
  return ssid != nullptr && other != nullptr && other[0] != '\0' && std::strcmp(ssid, other) == 0;
}

}  // namespace

bool locationAgeDays(const char* location, const char* record, const int year, const int month, const int day,
                     int& days) {
  days = 0;
  if (location == nullptr || location[0] == '\0') return false;
  const LocationFix fix = describeLocation(record, location);
  if (fix.year == 0) return false;
  days = static_cast<int>(daysFromCivil(year, month, day) - daysFromCivil(fix.year, fix.month, fix.day));
  return true;
}

Decision decide(const Situation& s) {
  if (!s.enabled) return Decision::Off;
  if (!s.jobOnline) return Decision::JobOffline;
  if (!s.connected) return Decision::NotConnected;
  if (sameNetwork(s.ssid, s.peerSsid) || sameNetwork(s.ssid, s.hubSsid)) return Decision::DeviceNetwork;
  if (!s.clockValid || s.year < 2000 || s.month < 1 || s.month > 12 || s.day < 1 || s.day > 31) {
    return Decision::NoClock;
  }
  if (s.triedYmd != 0 && s.triedYmd == ymd(s.year, s.month, s.day)) return Decision::AlreadyTried;
  int days = 0;
  if (locationAgeDays(s.location, s.record, s.year, s.month, s.day, days) && days == 0) return Decision::Fresh;
  return Decision::Run;
}

const char* decisionName(const Decision d) {
  switch (d) {
    case Decision::Run:
      return "run";
    case Decision::Off:
      return "off";
    case Decision::JobOffline:
      return "job-offline";
    case Decision::AlreadyTried:
      return "tried-today";
    case Decision::NotConnected:
      return "not-connected";
    case Decision::DeviceNetwork:
      return "device-network";
    case Decision::NoClock:
      return "no-clock";
    case Decision::Fresh:
      return "fresh";
  }
  return "?";
}

Verdict judgeFix(const bool fromWifi, const double lat, const double lon, const uint32_t accuracyM) {
  if (!fromWifi) return Verdict::NotWifi;
  if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 ||
      (lat == 0.0 && lon == 0.0) || accuracyM == 0) {
    return Verdict::Invalid;
  }
  if (accuracyM > MAX_ACCURACY_M) return Verdict::TooVague;
  return Verdict::Save;
}

bool keepTighterFix(const char* location, const char* record, double& lat, double& lon, uint32_t& accuracyM) {
  if (location == nullptr || location[0] == '\0') return false;
  const LocationFix old = describeLocation(record, location);
  if ((old.source != LocationSource::Wifi && old.source != LocationSource::WifiAuto) || old.accuracyM == 0 ||
      old.accuracyM > accuracyM) {
    return false;
  }
  double oldLat = 0.0;
  double oldLon = 0.0;
  if (!parseLocation(old.location, oldLat, oldLon)) return false;
  // Metres apart, flat-earth: exact enough over the ~2 km this compares.
  constexpr double M_PER_DEG = 111195.0;
  constexpr double RAD_PER_DEG = 0.017453292519943295;
  const double north = (lat - oldLat) * M_PER_DEG;
  const double east = (lon - oldLon) * M_PER_DEG * std::cos((lat + oldLat) * 0.5 * RAD_PER_DEG);
  const double reach = static_cast<double>(old.accuracyM) + static_cast<double>(accuracyM);
  if (north * north + east * east > reach * reach) return false;
  lat = oldLat;
  lon = oldLon;
  accuracyM = old.accuracyM;
  return true;
}

bool autoRecord(const double lat, const double lon, const uint32_t accuracyM, const int year, const int month,
                const int day, char* location, const size_t locationCap, char* record, const size_t recordCap) {
  if (location == nullptr || locationCap == 0 || record == nullptr || recordCap == 0) return false;
  location[0] = '\0';
  record[0] = '\0';
  LocationFix fix;
  fix.source = LocationSource::WifiAuto;
  fix.accuracyM = accuracyM;
  fix.year = static_cast<uint16_t>(year);
  fix.month = static_cast<uint8_t>(month);
  fix.day = static_cast<uint8_t>(day);
  // The same checks a typed entry passes, so every card reads it the same way.
  char formatted[LOCATION_CAP];
  if (!formatLocation(lat, lon, formatted, sizeof(formatted)) ||
      !normalizeLocation(formatted, fix.location, sizeof(fix.location)) || fix.location[0] == '\0') {
    return false;
  }
  if (std::strlen(fix.location) >= locationCap || !formatLocationFix(fix, record, recordCap)) {
    record[0] = '\0';
    return false;
  }
  std::snprintf(location, locationCap, "%s", fix.location);
  return true;
}

}  // namespace sleepcards::autolocate
