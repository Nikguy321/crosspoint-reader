#include "WeatherCache.h"

#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace weather {
namespace {

// ---- writing -----------------------------------------------------------------------------------

class Writer {
 public:
  Writer(char* out, const size_t cap) : out_(out), cap_(cap) {
    if (cap_ > 0) out_[0] = '\0';
  }
  void put(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (failed_) return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(out_ + len_, cap_ - len_, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= cap_ - len_) {
      failed_ = true;
      return;
    }
    len_ += static_cast<size_t>(n);
  }
  // " <v>" or " -".
  void value(const int16_t v) {
    if (v == NO_VALUE) {
      put(" -");
    } else {
      put(" %d", static_cast<int>(v));
    }
  }
  void stamp(const int64_t t) {
    if (t == 0) {
      put(" -");
    } else {
      put(" %" PRId64, t);
    }
  }
  size_t finish() {
    if (failed_ || len_ >= cap_) {
      if (cap_ > 0) out_[0] = '\0';
      return 0;
    }
    return len_;
  }

 private:
  char* out_;
  size_t cap_;
  size_t len_ = 0;
  bool failed_ = false;
};

// A text field on one line: printable ASCII only.
void putText(Writer& w, const char tag, const char* text) {
  char clean[HEADLINE_CAP];
  size_t n = 0;
  for (size_t i = 0; text[i] != '\0' && n + 1 < sizeof(clean); i++) {
    const auto c = static_cast<unsigned char>(text[i]);
    clean[n++] = c >= 0x20 && c < 0x7F ? static_cast<char>(c) : ' ';
  }
  clean[n] = '\0';
  w.put("%c %s\n", tag, clean);
}

constexpr const char* PLACE_WORDS[] = {"typed", "wifi", "ip", "wifi-auto", "phone"};
constexpr uint8_t PLACE_COUNT = 5;

// ---- reading -----------------------------------------------------------------------------------

// The text as lines: next() hands out each line (NUL-terminated in a private copy).
class Lines {
 public:
  Lines(const char* text, const size_t len) : text_(text), len_(len) {}
  bool next(char* line, const size_t cap) {
    if (pos_ >= len_) return false;
    size_t n = 0;
    while (pos_ < len_ && text_[pos_] != '\n') {
      const char c = text_[pos_++];
      if (c == '\0' || n + 1 >= cap) {
        bad_ = true;
        return false;
      }
      line[n++] = c;
    }
    if (pos_ >= len_) {
      bad_ = true;  // every line ends in '\n'
      return false;
    }
    pos_++;
    line[n] = '\0';
    return true;
  }
  bool bad() const { return bad_; }

 private:
  const char* text_;
  size_t len_;
  size_t pos_ = 0;
  bool bad_ = false;
};

// Space-separated fields of one line, read strictly.
class Fields {
 public:
  explicit Fields(const char* line) : p_(line) {}
  bool word(char* out, const size_t cap) {
    if (*p_ != ' ') return false;
    p_++;
    size_t n = 0;
    while (*p_ != '\0' && *p_ != ' ') {
      if (n + 1 >= cap) return false;
      out[n++] = *p_++;
    }
    out[n] = '\0';
    return n > 0;
  }
  bool integer(int64_t& v, const int64_t lo, const int64_t hi) {
    char w[24];
    if (!word(w, sizeof(w))) return false;
    char* end = nullptr;
    const long long x = std::strtoll(w, &end, 10);
    if (end == w || *end != '\0' || x < lo || x > hi) return false;
    v = x;
    return true;
  }
  // A fixed-point value or "-".
  bool value(int16_t& v) {
    char w[8];
    if (!word(w, sizeof(w))) return false;
    if (std::strcmp(w, "-") == 0) {
      v = NO_VALUE;
      return true;
    }
    char* end = nullptr;
    const long x = std::strtol(w, &end, 10);
    if (end == w || *end != '\0' || x < -32767 || x > 32767) return false;
    v = static_cast<int16_t>(x);
    return true;
  }
  // A UTC stamp or "-" (0).
  bool stamp(int64_t& t) {
    char w[24];
    if (!word(w, sizeof(w))) return false;
    if (std::strcmp(w, "-") == 0) {
      t = 0;
      return true;
    }
    char* end = nullptr;
    const long long x = std::strtoll(w, &end, 10);
    if (end == w || *end != '\0' || x < 0 || x > 4102444800LL) return false;
    t = x;
    return true;
  }
  bool coordinate(double& v, const double limit) {
    char w[16];
    if (!word(w, sizeof(w))) return false;
    char* end = nullptr;
    const double x = std::strtod(w, &end);
    if (end == w || *end != '\0' || !std::isfinite(x) || std::fabs(x) > limit) return false;
    v = x;
    return true;
  }
  bool done() const { return *p_ == '\0'; }

 private:
  const char* p_;
};

bool parseHeaderLine(const char* line, Record* record, Header& header) {
  if (std::strncmp(line, "W1", 2) != 0) return false;
  Fields f(line + 2);
  int64_t fetch = 0;
  int64_t trusted = 0;
  int64_t lastDay = 0;
  double lat = 0;
  double lon = 0;
  if (!f.integer(fetch, 1, 4102444800LL) || !f.integer(trusted, 0, 1) || !f.coordinate(lat, 90.0) ||
      !f.coordinate(lon, 180.0) || !f.integer(lastDay, 1, 4102444800LL) || !f.done()) {
    return false;
  }
  header.fetchUtc = fetch;
  header.lat = lat;
  header.lon = lon;
  header.lastDayUtc = lastDay;
  if (record != nullptr) {
    record->fetchUtc = fetch;
    record->clockTrusted = trusted == 1;
    record->lat = lat;
    record->lon = lon;
  }
  return true;
}

}  // namespace

size_t encodeCache(const Record& r, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  const Forecast& f = r.forecast;
  if (f.dayCount == 0 || f.dayCount > MAX_DAYS || f.hourCount > MAX_HOURS || r.fetchUtc <= 0 ||
      r.placeSource >= PLACE_COUNT || r.alerts.count > MAX_ALERTS) {
    out[0] = '\0';
    return 0;
  }
  Writer w(out, cap);
  w.put("W1 %" PRId64 " %d %.2f %.2f %" PRId64 "\n", r.fetchUtc, r.clockTrusted ? 1 : 0, r.lat, r.lon,
        f.days[f.dayCount - 1].t);
  w.put("P %s", PLACE_WORDS[r.placeSource]);
  if (r.placeYmd == 0) {
    w.put(" -");
  } else {
    w.put(" %08" PRIu32, r.placeYmd);
  }
  w.put(" %" PRId32 "\n", f.utcOffsetS);
  const Current& c = f.current;
  if (c.valid) {
    w.put("C");
    for (const int16_t v :
         {c.tempC10, c.feelsC10, c.humidity, c.pressureHpa10, c.windMs10, c.gustMs10, c.windDir, c.code}) {
      w.value(v);
    }
    w.put("\n");
  } else {
    w.put("C -\n");
  }
  for (uint8_t i = 0; i < f.hourCount; i++) {
    const Hour& h = f.hours[i];
    w.put("H %" PRId64, h.t);
    for (const int16_t v : {h.tempC10, h.pop, h.code, h.windMs10}) w.value(v);
    w.put("\n");
  }
  for (uint8_t i = 0; i < f.dayCount; i++) {
    const Day& d = f.days[i];
    w.put("D %" PRId64, d.t);
    for (const int16_t v : {d.code, d.maxC10, d.minC10, d.pop, d.precipMm10, d.windMaxMs10, d.gustMaxMs10, d.windDir}) {
      w.value(v);
    }
    w.put("\n");
  }
  const Alerts& a = r.alerts;
  w.put("N %u", static_cast<unsigned>(a.status));
  w.stamp(a.asOf);
  w.put(" %u %d\n", static_cast<unsigned>(a.total), a.recheckFailed ? 1 : 0);
  for (uint8_t i = 0; i < a.count; i++) {
    const Alert& al = a.list[i];
    w.put("A %u %u %u", static_cast<unsigned>(al.severity), static_cast<unsigned>(al.urgency),
          static_cast<unsigned>(al.type));
    w.stamp(al.onset);
    w.stamp(al.ends);
    w.stamp(al.expires);
    w.put("\n");
    putText(w, 'E', al.event);
    putText(w, 'L', al.headline);
  }
  w.put("end\n");
  return w.finish();
}

bool decodeHeader(const char* text, const size_t len, Header& header) {
  header = Header{};
  if (text == nullptr) return false;
  char line[HEADER_CAP];
  size_t n = 0;
  while (n < len && text[n] != '\n') {
    if (text[n] == '\0' || n + 1 >= sizeof(line)) return false;
    line[n] = text[n];
    n++;
  }
  if (n >= len) return false;  // no end of line
  line[n] = '\0';
  Header h;
  if (!parseHeaderLine(line, nullptr, h)) return false;
  header = h;
  return true;
}

namespace {
bool decodeInto(const char* text, const size_t len, Record& r) {
  if (text == nullptr || len == 0 || len > CACHE_CAP) return false;
  Lines lines(text, len);
  char line[HEADLINE_CAP + 8];
  Header header;
  if (!lines.next(line, sizeof(line)) || !parseHeaderLine(line, &r, header)) return false;

  bool place = false;
  bool current = false;
  bool status = false;
  bool ended = false;
  int alertStage = 0;  // 0 = expecting A, 1 = E, 2 = L
  Forecast& f = r.forecast;
  while (lines.next(line, sizeof(line))) {
    if (ended) return false;  // nothing after "end"
    if (std::strcmp(line, "end") == 0) {
      ended = true;
      continue;
    }
    if (line[0] == '\0' || (line[1] != ' ' && line[1] != '\0')) return false;
    const char tag = line[0];
    if (alertStage != 0 && tag != (alertStage == 1 ? 'E' : 'L')) return false;
    Fields fl(line + 1);
    switch (tag) {
      case 'P': {
        char word[16];
        if (place || !fl.word(word, sizeof(word))) return false;
        uint8_t source = PLACE_COUNT;
        for (uint8_t i = 0; i < PLACE_COUNT; i++) {
          if (std::strcmp(word, PLACE_WORDS[i]) == 0) source = i;
        }
        char ymd[12];
        int64_t offset = 0;
        if (source >= PLACE_COUNT || !fl.word(ymd, sizeof(ymd)) || !fl.integer(offset, -18 * 3600, 18 * 3600) ||
            !fl.done()) {
          return false;
        }
        if (std::strcmp(ymd, "-") != 0) {
          char* end = nullptr;
          const unsigned long v = std::strtoul(ymd, &end, 10);
          if (std::strlen(ymd) != 8 || *end != '\0' || v < 20000101UL || v > 20991231UL) return false;
          r.placeYmd = static_cast<uint32_t>(v);
        }
        r.placeSource = source;
        f.utcOffsetS = static_cast<int32_t>(offset);
        place = true;
        break;
      }
      case 'C': {
        if (current) return false;
        current = true;
        if (std::strcmp(line, "C -") == 0) break;
        Current& c = f.current;
        for (int16_t* v :
             {&c.tempC10, &c.feelsC10, &c.humidity, &c.pressureHpa10, &c.windMs10, &c.gustMs10, &c.windDir, &c.code}) {
          if (!fl.value(*v)) return false;
        }
        if (!fl.done() || c.tempC10 == NO_VALUE) return false;
        c.valid = true;
        break;
      }
      case 'H': {
        if (f.hourCount >= MAX_HOURS || f.dayCount > 0) return false;
        Hour& h = f.hours[f.hourCount];
        if (!fl.stamp(h.t) || h.t == 0) return false;
        for (int16_t* v : {&h.tempC10, &h.pop, &h.code, &h.windMs10}) {
          if (!fl.value(*v)) return false;
        }
        if (!fl.done() || (f.hourCount > 0 && h.t <= f.hours[f.hourCount - 1].t)) return false;
        f.hourCount++;
        break;
      }
      case 'D': {
        if (f.dayCount >= MAX_DAYS || status) return false;
        Day& d = f.days[f.dayCount];
        if (!fl.stamp(d.t) || d.t == 0) return false;
        for (int16_t* v :
             {&d.code, &d.maxC10, &d.minC10, &d.pop, &d.precipMm10, &d.windMaxMs10, &d.gustMaxMs10, &d.windDir}) {
          if (!fl.value(*v)) return false;
        }
        if (!fl.done() || (f.dayCount > 0 && d.t <= f.days[f.dayCount - 1].t)) return false;
        f.dayCount++;
        break;
      }
      case 'N': {
        int64_t st = 0;
        int64_t total = 0;
        int64_t failed = 0;
        Alerts& a = r.alerts;
        if (status || !fl.integer(st, 0, static_cast<int64_t>(AlertsStatus::TooMany)) || !fl.stamp(a.asOf) ||
            !fl.integer(total, 0, UINT16_MAX) || !fl.integer(failed, 0, 1) || !fl.done()) {
          return false;
        }
        a.status = static_cast<AlertsStatus>(st);
        a.total = static_cast<uint16_t>(total);
        a.recheckFailed = failed == 1;
        status = true;
        break;
      }
      case 'A': {
        Alerts& a = r.alerts;
        if (!status || a.count >= MAX_ALERTS) return false;
        Alert& al = a.list[a.count];
        int64_t sev = 0;
        int64_t urg = 0;
        int64_t type = 0;
        if (!fl.integer(sev, 0, static_cast<int64_t>(Severity::Extreme)) ||
            !fl.integer(urg, 0, static_cast<int64_t>(Urgency::Immediate)) ||
            !fl.integer(type, 0, static_cast<int64_t>(MessageType::Update)) || !fl.stamp(al.onset) ||
            !fl.stamp(al.ends) || !fl.stamp(al.expires) || !fl.done()) {
          return false;
        }
        al.severity = static_cast<Severity>(sev);
        al.urgency = static_cast<Urgency>(urg);
        al.type = static_cast<MessageType>(type);
        alertStage = 1;
        break;
      }
      case 'E':
      case 'L': {
        // Only right after its A line (E) or its E line (L): the alert being read is in range.
        if (alertStage != (tag == 'E' ? 1 : 2) || r.alerts.count >= MAX_ALERTS) return false;
        Alert& al = r.alerts.list[r.alerts.count];
        const char* text = line[1] == ' ' ? line + 2 : line + 1;
        if (tag == 'E') {
          if (text[0] == '\0') return false;
          std::snprintf(al.event, sizeof(al.event), "%s", text);
          alertStage = 2;
        } else {
          std::snprintf(al.headline, sizeof(al.headline), "%s", text);
          alertStage = 0;
          r.alerts.count++;
        }
        break;
      }
      default:
        return false;
    }
  }
  if (lines.bad() || !ended || !place || !current || !status || alertStage != 0 || f.dayCount == 0) return false;
  if (f.days[f.dayCount - 1].t != header.lastDayUtc) return false;
  // Only a list has entries, and no more than it counted.
  return r.alerts.count <= r.alerts.total && (r.alerts.count == 0 || r.alerts.status == AlertsStatus::Listed);
}
}  // namespace

bool decodeCache(const char* text, const size_t len, Record& out) {
  resetInPlace(out);
  if (decodeInto(text, len, out)) return true;
  resetInPlace(out);
  return false;
}

}  // namespace weather
