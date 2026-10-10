#include "GeolocateProtocol.h"

#include <Memory.h>
#include <StreamingJsonParser.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace geolocate {
namespace {

bool isJsonSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

char lowerAscii(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

bool endsWithNoCase(const char* text, const char* suffix) {
  const size_t n = std::strlen(text);
  const size_t m = std::strlen(suffix);
  if (m > n) return false;
  for (size_t i = 0; i < m; i++) {
    if (lowerAscii(text[n - m + i]) != suffix[i]) return false;
  }
  return true;
}

bool sameMac(const uint8_t* a, const uint8_t* b) { return std::memcmp(a, b, 6) == 0; }

// ---- JSON --------------------------------------------------------------------------------------

// A JSON number exactly as the grammar spells it: -?(0|[1-9]d*)(.d+)?([eE][+-]?d+)?. The
// streaming parser hands over any run of [-+.eE0-9]; strtod would also take hex, "inf" and
// "nan" from other text, so the grammar is checked first.
bool parseJsonNumber(const char* text, const size_t len, double& out) {
  size_t i = 0;
  if (i < len && text[i] == '-') i++;
  if (i >= len || text[i] < '0' || text[i] > '9') return false;
  if (text[i] == '0') {
    i++;
  } else {
    while (i < len && text[i] >= '0' && text[i] <= '9') i++;
  }
  if (i < len && text[i] == '.') {
    i++;
    const size_t start = i;
    while (i < len && text[i] >= '0' && text[i] <= '9') i++;
    if (i == start) return false;
  }
  if (i < len && (text[i] == 'e' || text[i] == 'E')) {
    i++;
    if (i < len && (text[i] == '+' || text[i] == '-')) i++;
    const size_t start = i;
    while (i < len && text[i] >= '0' && text[i] <= '9') i++;
    if (i == start) return false;
  }
  if (i != len || len >= 32) return false;
  char buf[32];
  std::memcpy(buf, text, len);
  buf[len] = '\0';
  const double value = std::strtod(buf, nullptr);
  if (!std::isfinite(value)) return false;
  out = value;
  return true;
}

bool validCoordinates(const double lat, const double lon) {
  if (!(lat >= -90.0 && lat <= 90.0) || !(lon >= -180.0 && lon <= 180.0)) return false;
  // 0,0 is what a lookup that found nothing tends to report.
  return !(lat == 0.0 && lon == 0.0);
}

constexpr size_t KEY_CAP = 16;
constexpr size_t MAX_DEPTH = 8;

// Follows the object path of each value: keys[d] is the current key of the container at depth d
// (1 = the root object). A value's key is keys[depth], its parent's keys[depth - 1]. In an object
// every value must follow a key of its own and every key must get a value: the tokenizer drops a
// key (or a string value) too long for its buffer without saying so, and without this check the
// next value would be read as the previous key's.
struct PathTracker {
  char keys[MAX_DEPTH + 1][KEY_CAP] = {};
  bool isArray[MAX_DEPTH + 1] = {};
  bool keyPending[MAX_DEPTH + 1] = {};  // a key at this depth still waits for its value
  size_t depth = 0;
  bool rootOpened = false;
  bool rootClosed = false;
  // A second root, a close without an open, too deep, a value outside the root, a value without
  // its key or a key without its value.
  bool bad = false;

  // A value (scalar or container) arrives at the current depth: it takes the pending key.
  void takeKey() {
    if (depth == 0 || isArray[depth]) return;
    if (!keyPending[depth]) bad = true;
    keyPending[depth] = false;
  }
  bool enter(const bool array) {
    if (rootClosed || depth >= MAX_DEPTH || (depth == 0 && (array || rootOpened))) {
      bad = true;
      return false;
    }
    takeKey();
    if (depth == 0) rootOpened = true;
    depth++;
    keys[depth][0] = '\0';
    isArray[depth] = array;
    keyPending[depth] = false;
    return true;
  }
  void leave() {
    if (depth == 0) {
      bad = true;
      return;
    }
    if (keyPending[depth]) bad = true;
    depth--;
    if (depth == 0) rootClosed = true;
  }
  void key(const char* k, const size_t len) {
    if (depth == 0 || isArray[depth] || keyPending[depth]) {
      bad = true;
      return;
    }
    keyPending[depth] = true;
    if (len < KEY_CAP) {
      std::memcpy(keys[depth], k, len);
      keys[depth][len] = '\0';
    } else {
      std::snprintf(keys[depth], KEY_CAP, "%s", "~");  // too long for any key read here
    }
  }
  // A scalar at the current position: false when it sits outside the root object.
  bool scalar() {
    if (depth == 0) {
      bad = true;
      return false;
    }
    takeKey();
    return true;
  }
  // The value just seen is <parent>.<name> in an object (parent "" = the root).
  bool at(const char* parent, const char* name) const {
    if (depth == 0 || isArray[depth] || std::strcmp(keys[depth], name) != 0) return false;
    if (parent[0] == '\0') return depth == 1;
    return depth == 2 && !isArray[1] && std::strcmp(keys[1], parent) == 0;
  }
  bool complete() const { return rootOpened && rootClosed && !bad; }
};

// The body is one object and nothing else: the tokenizer skips stray characters, so text before
// the '{' or after the closing '}' is checked here.
bool singleObjectText(const char* body, const size_t len) {
  size_t first = 0;
  while (first < len && isJsonSpace(body[first])) first++;
  size_t last = len;
  while (last > first && isJsonSpace(body[last - 1])) last--;
  return last - first >= 2 && body[first] == '{' && body[last - 1] == '}';
}

struct BeaconCtx {
  PathTracker path;
  bool haveLat = false;
  bool haveLon = false;
  bool haveAccuracy = false;
  bool error = false;
  bool fallback = false;
  double lat = 0;
  double lon = 0;
  double accuracy = 0;
};

template <typename Ctx>
bool runParser(const char* body, const size_t len, Ctx& ctx, void (*onKey)(void*, const char*, size_t),
               void (*onString)(void*, const char*, size_t), void (*onNumber)(void*, const char*, size_t),
               void (*onBool)(void*, bool)) {
  JsonCallbacks cb{};
  cb.ctx = &ctx;
  cb.onKey = onKey;
  cb.onString = onString;
  cb.onNumber = onNumber;
  cb.onBool = onBool;
  cb.onNull = [](void* c) { static_cast<Ctx*>(c)->path.scalar(); };
  cb.onObjectStart = [](void* c) { static_cast<Ctx*>(c)->path.enter(false); };
  cb.onObjectEnd = [](void* c) { static_cast<Ctx*>(c)->path.leave(); };
  cb.onArrayStart = [](void* c) { static_cast<Ctx*>(c)->path.enter(true); };
  cb.onArrayEnd = [](void* c) { static_cast<Ctx*>(c)->path.leave(); };
  if (!singleObjectText(body, len)) return false;
  // ~560 bytes of token buffer and state: kept off the stack.
  auto parser = makeUniqueNoThrow<StreamingJsonParser>(cb);
  if (!parser) return false;
  parser->feed(body, len);
  return !parser->hasError() && ctx.path.complete();
}

}  // namespace

bool usableAccessPoint(const AccessPoint& ap) {
  if (ap.ssid[0] == '\0') return false;
  if ((ap.mac[0] & 0x03) != 0) return false;  // locally administered or group address
  static constexpr uint8_t ZERO[6] = {};
  if (sameMac(ap.mac, ZERO)) return false;
  // VRRP's virtual router addresses (RFC 5798: IPv4 00:00:5e:00:01:xx, IPv6 00:00:5e:00:02:xx).
  if (ap.mac[0] == 0x00 && ap.mac[1] == 0x00 && ap.mac[2] == 0x5e && ap.mac[3] == 0x00 &&
      (ap.mac[4] == 0x01 || ap.mac[4] == 0x02)) {
    return false;
  }
  // The SSID is at most 32 bytes; an unterminated one is refused rather than read past.
  if (std::memchr(ap.ssid, '\0', SSID_CAP) == nullptr) return false;
  return !endsWithNoCase(ap.ssid, "_nomap") && !endsWithNoCase(ap.ssid, "_optout");
}

bool sameDevice(const uint8_t* a, const uint8_t* b) {
  if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) return false;
  const uint32_t x = (static_cast<uint32_t>(a[3]) << 16) | (static_cast<uint32_t>(a[4]) << 8) | a[5];
  const uint32_t y = (static_cast<uint32_t>(b[3]) << 16) | (static_cast<uint32_t>(b[4]) << 8) | b[5];
  return (x > y ? x - y : y - x) < SAME_DEVICE_SPAN;
}

Split buildSplitRequests(const AccessPoint* aps, const size_t count, const uint8_t* exclude, char* bodyA, char* bodyB,
                         const size_t cap) {
  Split split;
  if (bodyA == nullptr || bodyB == nullptr || cap == 0) return split;
  bodyA[0] = '\0';
  bodyB[0] = '\0';
  if (aps == nullptr) return split;

  // Rank: the strongest eligible access point not already taken, then the next; a BSSID already
  // taken (at an equal or stronger reading) is skipped, and so is every address of the device left
  // out.
  constexpr size_t MAX_RANKED = 2 * MAX_REQUEST_APS;
  size_t ranked[MAX_RANKED];
  size_t picked = 0;
  while (picked < MAX_RANKED) {
    size_t best = count;
    for (size_t i = 0; i < count; i++) {
      if (!usableAccessPoint(aps[i]) || aps[i].rssi < MIN_RSSI_DBM) continue;
      if (exclude != nullptr && sameDevice(aps[i].mac, exclude)) continue;
      bool taken = false;
      for (size_t k = 0; k < picked && !taken; k++) taken = sameMac(aps[ranked[k]].mac, aps[i].mac);
      if (taken) continue;
      if (best == count || aps[i].rssi > aps[best].rssi) best = i;
    }
    if (best == count) break;
    ranked[picked++] = best;
  }
  split.usable = picked;

  // Devices: rank positions joined when their BSSIDs are one device's (and through a shared
  // neighbour), each named by its strongest member, the first in rank order.
  static_assert(MAX_RANKED <= 255, "rank positions fit a byte");
  uint8_t device[MAX_RANKED];
  for (size_t k = 0; k < picked; k++) device[k] = static_cast<uint8_t>(k);
  for (size_t k = 1; k < picked; k++) {
    for (size_t j = 0; j < k; j++) {
      if (!sameDevice(aps[ranked[j]].mac, aps[ranked[k]].mac)) continue;
      const uint8_t from = device[k] > device[j] ? device[k] : device[j];
      const uint8_t to = device[k] > device[j] ? device[j] : device[k];
      for (size_t m = 0; m <= k; m++) {
        if (device[m] == from) device[m] = to;
      }
    }
  }
  // The half of each device: its order among the devices (by strongest member), alternately.
  bool inB[MAX_RANKED] = {};
  size_t devices = 0;
  for (size_t k = 0; k < picked; k++) {
    if (device[k] != k) continue;  // not a device's strongest member
    for (size_t m = k; m < picked; m++) {
      if (device[m] == k) inB[m] = devices % 2 == 1;
    }
    devices++;
  }
  split.devices = devices;
  if (devices < MIN_REQUEST_APS) return split;

  // One body from one half's ranks, strongest first.
  const auto build = [&](const bool half, char* out) -> size_t {
    size_t n = 0;
    size_t written = 0;
    int w = std::snprintf(out, cap, "%s",
                          "{\"considerIp\":false,\"fallbacks\":{\"ipf\":false,\"lacf\":false},\"wifiAccessPoints\":[");
    for (size_t k = 0; k < picked && w >= 0 && written < MAX_REQUEST_APS; k++) {
      if (inB[k] != half) continue;
      n += static_cast<size_t>(w);
      if (n >= cap) return 0;
      const AccessPoint& ap = aps[ranked[k]];
      const int rssi = ap.rssi > 0 ? 0 : ap.rssi;
      w = std::snprintf(out + n, cap - n, "%s{\"macAddress\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"signalStrength\":%d}",
                        written == 0 ? "" : ",", static_cast<unsigned>(ap.mac[0]), static_cast<unsigned>(ap.mac[1]),
                        static_cast<unsigned>(ap.mac[2]), static_cast<unsigned>(ap.mac[3]),
                        static_cast<unsigned>(ap.mac[4]), static_cast<unsigned>(ap.mac[5]), rssi);
      written++;
    }
    if (w < 0) return 0;
    n += static_cast<size_t>(w);
    if (n >= cap) return 0;
    w = std::snprintf(out + n, cap - n, "%s", "]}");
    if (w < 0 || n + static_cast<size_t>(w) >= cap) return 0;
    return written;
  };
  const size_t inA = build(false, bodyA);
  const size_t inBody = build(true, bodyB);
  if (inA == 0 || inBody == 0) {
    bodyA[0] = '\0';
    bodyB[0] = '\0';
    return split;
  }
  split.halfA = inA;
  split.halfB = inBody;
  split.built = true;
  return split;
}

bool parseBeaconDbResponse(const char* body, const size_t len, Fix& out) {
  out = Fix{};
  if (body == nullptr || len == 0) return false;
  auto ctx = makeUniqueNoThrow<BeaconCtx>();
  if (!ctx) return false;
  const bool parsed = runParser(
      body, len, *ctx, [](void* c, const char* k, const size_t n) { static_cast<BeaconCtx*>(c)->path.key(k, n); },
      [](void* c, const char*, size_t) {
        auto* x = static_cast<BeaconCtx*>(c);
        if (!x->path.scalar()) return;
        if (x->path.at("", "fallback")) x->fallback = true;
      },
      [](void* c, const char* v, const size_t n) {
        auto* x = static_cast<BeaconCtx*>(c);
        if (!x->path.scalar()) return;
        double value = 0;
        const bool ok = parseJsonNumber(v, n, value);
        if (x->path.at("location", "lat")) {
          x->haveLat = ok;
          x->lat = value;
        } else if (x->path.at("location", "lng")) {
          x->haveLon = ok;
          x->lon = value;
        } else if (x->path.at("", "accuracy")) {
          x->haveAccuracy = ok;
          x->accuracy = value;
        }
      },
      [](void* c, bool) { static_cast<BeaconCtx*>(c)->path.scalar(); });
  if (!parsed || !ctx->haveLat || !ctx->haveLon || !ctx->haveAccuracy || ctx->fallback) return false;
  if (!validCoordinates(ctx->lat, ctx->lon)) return false;
  if (!(ctx->accuracy > 0.0 && ctx->accuracy < 20000000.0)) return false;
  out.source = FixSource::Wifi;
  out.lat = ctx->lat;
  out.lon = ctx->lon;
  out.accuracyM = static_cast<uint32_t>(std::ceil(ctx->accuracy));
  return true;
}

double distanceM(const double lat1, const double lon1, const double lat2, const double lon2) {
  constexpr double EARTH_RADIUS_M = 6371008.8;
  constexpr double RAD_PER_DEG = 0.017453292519943295;
  const double p1 = lat1 * RAD_PER_DEG;
  const double p2 = lat2 * RAD_PER_DEG;
  const double dp = (lat2 - lat1) * RAD_PER_DEG;
  const double dl = (lon2 - lon1) * RAD_PER_DEG;
  const double s1 = std::sin(dp / 2);
  const double s2 = std::sin(dl / 2);
  double h = s1 * s1 + std::cos(p1) * std::cos(p2) * s2 * s2;
  if (h > 1.0) h = 1.0;
  return 2.0 * EARTH_RADIUS_M * std::asin(std::sqrt(h));
}

namespace {

// One half on its own: NotAsked when it is a usable, tight location (so far, so good), else why not.
WifiVerdict judgeHalf(const HalfAnswer& h) {
  if (h.reply == Reply::NoMemory) return WifiVerdict::NoMemory;
  if (h.reply != Reply::Answered) return WifiVerdict::Unreachable;
  if (h.status != 200 || h.fix.source != FixSource::Wifi || !validCoordinates(h.fix.lat, h.fix.lon) ||
      h.fix.accuracyM < 1) {
    return WifiVerdict::NotFound;
  }
  if (h.fix.accuracyM > MAX_HALF_ACCURACY_M) return WifiVerdict::Vague;
  return WifiVerdict::NotAsked;
}

}  // namespace

bool secondHalfNeeded(const HalfAnswer& first) { return judgeHalf(first) == WifiVerdict::NotAsked; }

WifiVerdict judgeWifi(const Split& split, const HalfAnswer& a, const HalfAnswer& b, Fix& out, uint32_t& apartM) {
  out = Fix{};
  apartM = 0;
  if (!split.built || split.devices < MIN_REQUEST_APS) return WifiVerdict::TooFew;
  // The first half decides alone when it fails (the second is then not asked).
  const WifiVerdict first = judgeHalf(a);
  if (first != WifiVerdict::NotAsked) return first;
  const WifiVerdict second = judgeHalf(b);
  if (second != WifiVerdict::NotAsked) return second;

  const double apart = distanceM(a.fix.lat, a.fix.lon, b.fix.lat, b.fix.lon);
  const uint32_t reach = a.fix.accuracyM > b.fix.accuracyM ? a.fix.accuracyM : b.fix.accuracyM;
  apartM = apart >= 4.0e9 ? 4000000000u : static_cast<uint32_t>(std::ceil(apart));
  if (apart > static_cast<double>(reach)) return WifiVerdict::Disagree;

  // The midpoint (across the date line too: the longitudes differ by at most a few hundred metres).
  double dLon = b.fix.lon - a.fix.lon;
  if (dLon > 180.0) dLon -= 360.0;
  if (dLon < -180.0) dLon += 360.0;
  double lon = a.fix.lon + dLon / 2.0;
  if (lon > 180.0) lon -= 360.0;
  if (lon < -180.0) lon += 360.0;
  uint32_t accuracy = MIN_REPORTED_ACCURACY_M;
  if (reach > accuracy) accuracy = reach;
  if (apartM > accuracy) accuracy = apartM;
  out.source = FixSource::Wifi;
  out.lat = (a.fix.lat + b.fix.lat) / 2.0;
  out.lon = lon;
  out.accuracyM = accuracy;
  return WifiVerdict::Located;
}

const char* wifiVerdictName(const WifiVerdict v) {
  switch (v) {
    case WifiVerdict::NotAsked:
      return "not-asked";
    case WifiVerdict::Located:
      return "located";
    case WifiVerdict::TooFew:
      return "too-few-aps";
    case WifiVerdict::NoMemory:
      return "no-memory";
    case WifiVerdict::Unreachable:
      return "unreachable";
    case WifiVerdict::NotFound:
      return "no-fix";
    case WifiVerdict::Vague:
      return "too-vague";
    case WifiVerdict::Disagree:
      return "disagree";
  }
  return "?";
}

}  // namespace geolocate
