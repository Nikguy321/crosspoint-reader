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

void appendUtf8(char* out, size_t& n, const size_t cap, const uint32_t cp) {
  char enc[4];
  size_t k = 0;
  if (cp < 0x80) {
    enc[k++] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    enc[k++] = static_cast<char>(0xC0 | (cp >> 6));
    enc[k++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    enc[k++] = static_cast<char>(0xE0 | (cp >> 12));
    enc[k++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    enc[k++] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  if (n + k >= cap) return;
  std::memcpy(out + n, enc, k);
  n += k;
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  const char l = lowerAscii(c);
  if (l >= 'a' && l <= 'f') return l - 'a' + 10;
  return -1;
}

// A place name for the screen: the parser leaves \uXXXX escapes as text, so they are decoded here
// (a surrogate becomes '?'); control characters are dropped, runs of spaces collapsed, and the
// result is cut at a whole UTF-8 character.
void cleanPlace(const char* text, const size_t len, char* out, const size_t cap) {
  size_t n = 0;
  out[0] = '\0';
  for (size_t i = 0; i < len && n + 1 < cap;) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == '\\' && i + 5 < len && text[i + 1] == 'u') {
      uint32_t cp = 0;
      bool ok = true;
      for (size_t k = 0; k < 4; k++) {
        const int h = hexValue(text[i + 2 + k]);
        if (h < 0) ok = false;
        cp = cp * 16 + static_cast<uint32_t>(h < 0 ? 0 : h);
      }
      if (ok) {
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';
        if (cp >= 0x20) appendUtf8(out, n, cap, cp);
        i += 6;
        continue;
      }
    }
    if (c < 0x20 || c == 0x7F) {
      i++;
      continue;
    }
    if (c == ' ' && (n == 0 || out[n - 1] == ' ')) {
      i++;
      continue;
    }
    // Copy one whole UTF-8 sequence, or stop.
    size_t seq = 1;
    if (c >= 0xF0) {
      seq = 4;
    } else if (c >= 0xE0) {
      seq = 3;
    } else if (c >= 0xC0) {
      seq = 2;
    }
    if (i + seq > len || n + seq >= cap) break;
    std::memcpy(out + n, text + i, seq);
    n += seq;
    i += seq;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

// After a cut: drop a trailing UTF-8 sequence that lost its tail.
void trimPartialUtf8(char* s) {
  const size_t n = std::strlen(s);
  size_t lead = n;
  while (lead > 0 && (static_cast<unsigned char>(s[lead - 1]) & 0xC0) == 0x80) lead--;
  if (lead == 0) return;
  const auto c = static_cast<unsigned char>(s[lead - 1]);
  size_t want = 1;
  if (c >= 0xF0) {
    want = 4;
  } else if (c >= 0xE0) {
    want = 3;
  } else if (c >= 0xC0) {
    want = 2;
  }
  if (n - (lead - 1) < want) s[lead - 1] = '\0';
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

struct IpCtx {
  PathTracker path;
  bool haveLat = false;
  bool haveLon = false;
  bool success = false;
  double lat = 0;
  double lon = 0;
  char city[PLACE_CAP] = "";
  char region[PLACE_CAP] = "";
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
  // The SSID is at most 32 bytes; an unterminated one is refused rather than read past.
  if (std::memchr(ap.ssid, '\0', SSID_CAP) == nullptr) return false;
  return !endsWithNoCase(ap.ssid, "_nomap") && !endsWithNoCase(ap.ssid, "_optout");
}

size_t buildRequestBody(const AccessPoint* aps, const size_t count, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (aps == nullptr) return 0;

  // Pick the strongest usable access point not already taken, up to the limit; a BSSID already
  // taken (at an equal or stronger reading) is skipped.
  size_t chosen[MAX_REQUEST_APS];
  size_t picked = 0;
  while (picked < MAX_REQUEST_APS) {
    size_t best = count;
    for (size_t i = 0; i < count; i++) {
      if (!usableAccessPoint(aps[i])) continue;
      bool taken = false;
      for (size_t k = 0; k < picked && !taken; k++) taken = sameMac(aps[chosen[k]].mac, aps[i].mac);
      if (taken) continue;
      if (best == count || aps[i].rssi > aps[best].rssi) best = i;
    }
    if (best == count) break;
    chosen[picked++] = best;
  }
  if (picked < MIN_REQUEST_APS) return 0;

  size_t n = 0;
  int w = std::snprintf(out, cap, "%s", "{\"considerIp\":false,\"wifiAccessPoints\":[");
  for (size_t k = 0; k < picked && w >= 0; k++) {
    n += static_cast<size_t>(w);
    if (n >= cap) break;
    const AccessPoint& ap = aps[chosen[k]];
    const int rssi = ap.rssi > 0 ? 0 : (ap.rssi < -127 ? -127 : ap.rssi);
    w = std::snprintf(out + n, cap - n, "%s{\"macAddress\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"signalStrength\":%d}",
                      k == 0 ? "" : ",", static_cast<unsigned>(ap.mac[0]), static_cast<unsigned>(ap.mac[1]),
                      static_cast<unsigned>(ap.mac[2]), static_cast<unsigned>(ap.mac[3]),
                      static_cast<unsigned>(ap.mac[4]), static_cast<unsigned>(ap.mac[5]), rssi);
  }
  if (w >= 0 && n < cap) {
    n += static_cast<size_t>(w);
    if (n < cap) w = std::snprintf(out + n, cap - n, "%s", "]}");
    if (w >= 0) n += static_cast<size_t>(w);
  }
  if (w < 0 || n >= cap) {
    out[0] = '\0';
    return 0;
  }
  return picked;
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

bool parseIpWhoisResponse(const char* body, const size_t len, Fix& out) {
  out = Fix{};
  if (body == nullptr || len == 0) return false;
  auto ctx = makeUniqueNoThrow<IpCtx>();
  if (!ctx) return false;
  const bool parsed = runParser(
      body, len, *ctx, [](void* c, const char* k, const size_t n) { static_cast<IpCtx*>(c)->path.key(k, n); },
      [](void* c, const char* v, const size_t n) {
        auto* x = static_cast<IpCtx*>(c);
        if (!x->path.scalar()) return;
        if (x->path.at("", "city")) {
          cleanPlace(v, n, x->city, sizeof(x->city));
        } else if (x->path.at("", "region")) {
          cleanPlace(v, n, x->region, sizeof(x->region));
        }
      },
      [](void* c, const char* v, const size_t n) {
        auto* x = static_cast<IpCtx*>(c);
        if (!x->path.scalar()) return;
        double value = 0;
        const bool ok = parseJsonNumber(v, n, value);
        if (x->path.at("", "latitude")) {
          x->haveLat = ok;
          x->lat = value;
        } else if (x->path.at("", "longitude")) {
          x->haveLon = ok;
          x->lon = value;
        }
      },
      [](void* c, const bool value) {
        auto* x = static_cast<IpCtx*>(c);
        if (!x->path.scalar()) return;
        if (x->path.at("", "success")) x->success = value;
      });
  if (!parsed || !ctx->success || !ctx->haveLat || !ctx->haveLon) return false;
  if (!validCoordinates(ctx->lat, ctx->lon)) return false;
  out.source = FixSource::Ip;
  out.lat = ctx->lat;
  out.lon = ctx->lon;
  out.accuracyM = IP_ACCURACY_M;
  if (ctx->city[0] != '\0' && ctx->region[0] != '\0' && std::strcmp(ctx->city, ctx->region) != 0) {
    std::snprintf(out.place, sizeof(out.place), "%s, %s", ctx->city, ctx->region);
  } else {
    std::snprintf(out.place, sizeof(out.place), "%s", ctx->city[0] != '\0' ? ctx->city : ctx->region);
  }
  trimPartialUtf8(out.place);
  return true;
}

bool needsIpLookup(const Fix& wifi) { return !wifi.valid() || wifi.accuracyM > WIFI_GOOD_ACCURACY_M; }

const Fix* chooseFix(const Fix& wifi, const Fix& ip) {
  if (wifi.valid() && wifi.accuracyM <= WIFI_GOOD_ACCURACY_M) return &wifi;
  if (wifi.valid() && ip.valid()) return wifi.accuracyM <= ip.accuracyM ? &wifi : &ip;
  if (wifi.valid()) return &wifi;
  if (ip.valid()) return &ip;
  return nullptr;
}

LookupFailure classifyFailure(const size_t apsSent, const Reply beacon, const Reply ip) {
  if (ip == Reply::Answered) return apsSent < MIN_REQUEST_APS ? LookupFailure::TooFew : LookupFailure::NotFound;
  if (ip == Reply::NoMemory || beacon == Reply::NoMemory) return LookupFailure::NoMemory;
  if (beacon == Reply::Answered) return LookupFailure::NotFound;
  return LookupFailure::Unreachable;
}

}  // namespace geolocate
