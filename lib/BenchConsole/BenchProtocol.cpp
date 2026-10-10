#include "BenchProtocol.h"

#include <cstdio>
#include <cstring>

namespace bench {

namespace {
// Nibble table for the reflected 0xEDB88320 polynomial: 64 bytes of flash
// instead of the usual 1 KB byte table, ~2x the bitwise loop's speed.
constexpr uint32_t CRC_NIBBLE[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                     0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                     0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

constexpr char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr KeyDef KEYS[] = {
    {"back", KeyKind::Button, 0, 60, 1},
    {"confirm", KeyKind::Button, 1, 60, 1},
    {"left", KeyKind::Button, 2, 60, 1},
    {"right", KeyKind::Button, 3, 60, 1},
    {"up", KeyKind::Button, 4, 60, 1},
    {"down", KeyKind::Button, 5, 60, 1},
    {"power", KeyKind::Button, 6, 60, 1},
    {"powerdouble", KeyKind::Button, 6, DOUBLE_HOLD_MS, 2},
    {"home", KeyKind::Home, 0, 100, 1},
    {"homelong", KeyKind::Home, 0, 1000, 1},
    {"homedouble", KeyKind::Home, 0, DOUBLE_HOLD_MS, 2},
};

bool isSpace(const char c) { return c == ' ' || c == '\t'; }

char toLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

uint32_t crc32Update(uint32_t crc, const uint8_t* data, const size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ CRC_NIBBLE[crc & 0x0F];
    crc = (crc >> 4) ^ CRC_NIBBLE[crc & 0x0F];
  }
  return ~crc;
}

size_t base64Encode(const uint8_t* in, const size_t len, char* out, const size_t outCap) {
  const size_t needed = base64Length(len);
  if (outCap < needed + 1) return 0;
  size_t o = 0;
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    const uint32_t v = (static_cast<uint32_t>(in[i]) << 16) | (static_cast<uint32_t>(in[i + 1]) << 8) | in[i + 2];
    out[o++] = B64[(v >> 18) & 0x3F];
    out[o++] = B64[(v >> 12) & 0x3F];
    out[o++] = B64[(v >> 6) & 0x3F];
    out[o++] = B64[v & 0x3F];
  }
  const size_t rest = len - i;
  if (rest > 0) {
    uint32_t v = static_cast<uint32_t>(in[i]) << 16;
    if (rest == 2) v |= static_cast<uint32_t>(in[i + 1]) << 8;
    out[o++] = B64[(v >> 18) & 0x3F];
    out[o++] = B64[(v >> 12) & 0x3F];
    out[o++] = rest == 2 ? B64[(v >> 6) & 0x3F] : '=';
    out[o++] = '=';
  }
  out[o] = '\0';
  return o;
}

bool splitCommand(char* line, char*& verb, char*& rest) {
  if (line == nullptr || strncmp(line, "CMD:", 4) != 0) return false;
  char* p = line + 4;
  while (isSpace(*p)) ++p;
  verb = p;
  while (*p != '\0' && !isSpace(*p)) {
    if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - 'a' + 'A');
    ++p;
  }
  if (*p != '\0') {
    *p++ = '\0';
    while (isSpace(*p)) ++p;
  }
  rest = p;
  // Trailing spaces are never part of an argument (paths ending in a space are
  // rejected by isValidPath anyway); strip them so "PING  " still matches.
  size_t n = strlen(rest);
  while (n > 0 && isSpace(rest[n - 1])) rest[--n] = '\0';
  return true;
}

bool nextToken(char*& cursor, char*& token) {
  if (cursor == nullptr) return false;
  while (isSpace(*cursor)) ++cursor;
  if (*cursor == '\0') return false;
  token = cursor;
  while (*cursor != '\0' && !isSpace(*cursor)) ++cursor;
  if (*cursor != '\0') {
    *cursor++ = '\0';
    while (isSpace(*cursor)) ++cursor;
  }
  return true;
}

bool parseU32(const char* s, uint32_t& out) {
  if (s == nullptr || *s == '\0') return false;
  uint64_t v = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + static_cast<uint64_t>(*p - '0');
    if (v > 0xFFFFFFFFull) return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

bool parseI32(const char* s, int32_t& out) {
  if (s == nullptr) return false;
  const bool negative = *s == '-';
  uint32_t magnitude = 0;
  if (!parseU32(negative ? s + 1 : s, magnitude)) return false;
  if (magnitude > (negative ? 0x80000000u : 0x7FFFFFFFu)) return false;
  out = negative ? static_cast<int32_t>(0u - magnitude) : static_cast<int32_t>(magnitude);
  return true;
}

bool parseHex32(const char* s, uint32_t& out) {
  if (s == nullptr || *s == '\0') return false;
  uint32_t v = 0;
  size_t digits = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    const int h = hexValue(*p);
    if (h < 0 || ++digits > 8) return false;
    v = (v << 4) | static_cast<uint32_t>(h);
  }
  out = v;
  return true;
}

bool normalizeMd5Hex(char* s) {
  if (s == nullptr || strlen(s) != 32) return false;
  for (char* p = s; *p != '\0'; ++p) {
    if (hexValue(*p) < 0) return false;
    *p = toLower(*p);
  }
  return true;
}

bool isValidPath(const char* path, const bool allowRoot) {
  if (path == nullptr || path[0] != '/') return false;
  const size_t len = strlen(path);
  if (len > MAX_PATH_LEN) return false;
  if (len == 1) return allowRoot;
  for (size_t i = 0; i < len; ++i) {
    const auto c = static_cast<unsigned char>(path[i]);
    if (c < 0x20 || c == 0x7F || c == '\\') return false;
    // Characters FAT refuses in a name: SdFat would fail late with a vague error.
    if (c == '"' || c == '*' || c == ':' || c == '<' || c == '>' || c == '?' || c == '|') return false;
  }
  // Walk the components after the leading '/'.
  const char* p = path + 1;
  while (true) {
    const char* slash = strchr(p, '/');
    const size_t n = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (n == 0) return false;  // "//" or a trailing '/'
    if ((n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return false;
    // FAT trims trailing spaces and dots from names; refuse names it would alter.
    if (p[n - 1] == ' ' || p[n - 1] == '.') return false;
    if (!slash) break;
    p = slash + 1;
  }
  return true;
}

bool isPartPath(const char* path) {
  if (path == nullptr) return false;
  const size_t len = strlen(path);
  const size_t suffixLen = sizeof(PART_SUFFIX) - 1;
  if (len < suffixLen) return false;
  const char* tail = path + len - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i) {
    if (toLower(tail[i]) != PART_SUFFIX[i]) return false;
  }
  return true;
}

void trimTrailingSlashes(char* path) {
  if (path == nullptr) return;
  size_t n = strlen(path);
  while (n > 1 && path[n - 1] == '/') path[--n] = '\0';
}

bool parseChunkHeader(const char* line, const uint32_t maxLen, uint32_t& len, uint32_t& crc) {
  if (line == nullptr) return false;
  char buf[32];
  const size_t n = strlen(line);
  if (n == 0 || n >= sizeof(buf)) return false;
  memcpy(buf, line, n + 1);
  char* cursor = buf;
  char* lenTok = nullptr;
  char* crcTok = nullptr;
  char* extra = nullptr;
  if (!nextToken(cursor, lenTok) || !nextToken(cursor, crcTok) || nextToken(cursor, extra)) return false;
  uint32_t l = 0;
  uint32_t c = 0;
  if (!parseU32(lenTok, l) || !parseHex32(crcTok, c)) return false;
  if (l == 0 || l > maxLen) return false;
  len = l;
  crc = c;
  return true;
}

PutSession::Action PutSession::nak(const char* reason) {
  why = reason;
  if (++naks >= MAX_NAKS) {
    why = "naks";
    return Action::Abort;
  }
  return Action::Nak;
}

PutSession::Action PutSession::onHeader(const char* line, const bool lineOk) {
  if (line != nullptr && strncmp(line, "CMD:", 4) == 0) {
    // A new command while the upload is still open: the host gave up on it.
    why = "interrupted";
    return Action::Abort;
  }
  const uint32_t remaining = expected - received;
  const uint32_t maxLen = remaining < PUT_CHUNK_MAX ? remaining : static_cast<uint32_t>(PUT_CHUNK_MAX);
  if (!lineOk || !parseChunkHeader(line, maxLen, len, crc)) return nak("hdr");
  return Action::ReadPayload;
}

PutSession::Action PutSession::onPayload(const uint8_t* data, const size_t got) {
  if (got != len) return nak("short");
  if (crc32Update(0, data, len) != crc) return nak("crc");
  return Action::Write;
}

void PutSession::committed() {
  received += len;
  naks = 0;
}

bool parseSleepArgs(const char* args, SleepKind& out) {
  if (args == nullptr) args = "";
  while (isSpace(*args)) ++args;
  size_t n = strlen(args);
  while (n > 0 && isSpace(args[n - 1])) --n;
  if (n == 0) {
    out = SleepKind::Default;
    return true;
  }
  static constexpr char DEEP[] = "deep";
  if (n != sizeof(DEEP) - 1) return false;
  for (size_t i = 0; i < n; ++i) {
    char c = args[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != DEEP[i]) return false;
  }
  out = SleepKind::Deep;
  return true;
}

bool isValidSsid(const char* ssid) {
  if (ssid == nullptr || *ssid == '\0') return false;
  size_t n = 0;
  for (; ssid[n] != '\0'; ++n) {
    const auto c = static_cast<unsigned char>(ssid[n]);
    if (c < 0x20 || c == 0x7F || n >= SSID_MAX_BYTES) return false;
  }
  return true;
}

bool isValidNonce(const char* s) {
  if (s == nullptr || *s == '\0') return false;
  size_t n = 0;
  for (; s[n] != '\0'; ++n) {
    const char c = s[n];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
    if (!ok || n >= 16) return false;
  }
  return true;
}

bool lookupKey(const char* name, KeyDef& out) {
  if (name == nullptr) return false;
  for (const auto& key : KEYS) {
    const char* a = key.name;
    const char* b = name;
    while (*a != '\0' && *a == toLower(*b)) {
      ++a;
      ++b;
    }
    if (*a == '\0' && *b == '\0') {
      out = key;
      return true;
    }
  }
  return false;
}

uint32_t clampHoldMs(const uint32_t ms) {
  if (ms < HOLD_MIN_MS) return HOLD_MIN_MS;
  if (ms > HOLD_MAX_MS) return HOLD_MAX_MS;
  return ms;
}

bool parseCatArgs(char* args, uint32_t& tailBytes, char*& path) {
  tailBytes = CAT_TAIL_DEFAULT;
  if (args == nullptr) return false;
  char* p = args;
  while (*p >= '0' && *p <= '9') ++p;
  if (p != args && *p == ' ') {
    *p = '\0';
    uint32_t n = 0;
    if (!parseU32(args, n)) return false;
    tailBytes = n < 1 ? 1 : (n > CAT_TAIL_MAX ? CAT_TAIL_MAX : n);
    ++p;
    while (*p == ' ') ++p;
  } else {
    p = args;
  }
  if (*p == '\0') return false;
  path = p;
  return true;
}

namespace {
// Case-insensitive equality of a whole token.
bool tokenIs(const char* tok, const char* word) {
  while (*word != '\0' && toLower(*tok) == *word) {
    ++tok;
    ++word;
  }
  return *word == '\0' && *tok == '\0';
}
}  // namespace

bool parseWeatherArgs(const char* args, WeatherOp& out) {
  if (args == nullptr) args = "";
  while (isSpace(*args)) ++args;
  char word[8] = {};
  size_t n = 0;
  while (args[n] != '\0' && !isSpace(args[n])) {
    if (n + 1 >= sizeof(word)) return false;
    word[n] = args[n];
    ++n;
  }
  for (const char* rest = args + n; *rest != '\0'; ++rest) {
    if (!isSpace(*rest)) return false;
  }
  if (n == 0 || tokenIs(word, "show")) {
    out = WeatherOp::Show;
    return true;
  }
  if (tokenIs(word, "fetch")) {
    out = WeatherOp::Fetch;
    return true;
  }
  if (tokenIs(word, "clear")) {
    out = WeatherOp::Clear;
    return true;
  }
  return false;
}

bool parsePowerArgs(const char* args, PowerOp& out) {
  if (args == nullptr) args = "";
  char words[2][8] = {};
  size_t count = 0;
  while (true) {
    while (isSpace(*args)) ++args;
    if (*args == '\0') break;
    if (count == 2) return false;
    size_t n = 0;
    while (args[n] != '\0' && !isSpace(args[n])) {
      if (n + 1 >= sizeof(words[0])) return false;
      words[count][n] = args[n];
      ++n;
    }
    args += n;
    ++count;
  }
  if (count == 0) {
    out = PowerOp::Show;
    return true;
  }
  const char* mode = words[0];
  if (count == 2) {
    if (!tokenIs(words[0], "fake")) return false;
    mode = words[1];
    if (tokenIs(mode, "absent")) {
      out = PowerOp::FakeAbsent;
      return true;
    }
  }
  if (tokenIs(mode, "real")) {
    out = PowerOp::Real;
    return true;
  }
  return false;
}

namespace {
// Digits only, 1..maxDigits of them, at most maxValue; p moves past them.
bool takeNumber(const char*& p, const size_t maxDigits, const uint32_t maxValue, uint32_t& out) {
  size_t n = 0;
  uint32_t v = 0;
  while (p[n] >= '0' && p[n] <= '9') {
    if (n == maxDigits) return false;
    v = v * 10 + static_cast<uint32_t>(p[n] - '0');
    ++n;
  }
  if (n == 0 || v > maxValue) return false;
  p += n;
  out = v;
  return true;
}
}  // namespace

bool parseLocPhoneArgs(const char* args, LocPhoneArgs& out) {
  out = LocPhoneArgs{};
  if (args == nullptr) args = "";
  while (isSpace(*args)) ++args;
  size_t n = 0;
  while (args[n] != '\0' && !isSpace(args[n])) ++n;
  for (const char* rest = args + n; *rest != '\0'; ++rest) {
    if (!isSpace(*rest)) return false;
  }
  if (n == 0) return true;  // Show
  if (n == 3 && toLower(args[0]) == 'o' && toLower(args[1]) == 'f' && toLower(args[2]) == 'f') {
    out.op = LocPhoneOp::Off;
    return true;
  }
  const char* p = args;
  for (int i = 0; i < 4; ++i) {
    uint32_t octet = 0;
    if (!takeNumber(p, 3, 255, octet)) return false;
    out.ip[i] = static_cast<uint8_t>(octet);
    if (i < 3) {
      if (*p != '.') return false;
      ++p;
    }
  }
  if (*p == ':') {
    ++p;
    uint32_t port = 0;
    if (!takeNumber(p, 5, 65535, port) || port == 0) return false;
    out.port = static_cast<uint16_t>(port);
  }
  if (p != args + n) return false;
  const bool zero = out.ip[0] == 0 && out.ip[1] == 0 && out.ip[2] == 0 && out.ip[3] == 0;
  const bool broadcast = out.ip[0] == 255 && out.ip[1] == 255 && out.ip[2] == 255 && out.ip[3] == 255;
  if (zero || broadcast) return false;
  out.op = LocPhoneOp::Set;
  return true;
}

bool parseLocTestArgs(const char* args, bool& coords) {
  coords = false;
  if (args == nullptr) args = "";
  while (isSpace(*args)) ++args;
  char word[8] = {};
  size_t n = 0;
  while (args[n] != '\0' && !isSpace(args[n])) {
    if (n + 1 >= sizeof(word)) return false;
    word[n] = args[n];
    ++n;
  }
  for (const char* rest = args + n; *rest != '\0'; ++rest) {
    if (!isSpace(*rest)) return false;
  }
  if (n == 0) return true;
  if (!tokenIs(word, "coords")) return false;
  coords = true;
  return true;
}

bool parseAppArgs(const char* args, AppTarget& out) {
  if (args == nullptr) return false;
  while (isSpace(*args)) ++args;
  char word[16] = {};
  size_t n = 0;
  while (args[n] != '\0' && !isSpace(args[n])) {
    if (n + 1 >= sizeof(word)) return false;
    word[n] = args[n];
    ++n;
  }
  for (const char* rest = args + n; *rest != '\0'; ++rest) {
    if (!isSpace(*rest)) return false;
  }
  if (tokenIs(word, "apps")) {
    out = AppTarget::Apps;
    return true;
  }
  if (tokenIs(word, "wordsearch")) {
    out = AppTarget::WordSearch;
    return true;
  }
  if (tokenIs(word, "crossword")) {
    out = AppTarget::Crossword;
    return true;
  }
  if (tokenIs(word, "sudoku")) {
    out = AppTarget::Sudoku;
    return true;
  }
  if (tokenIs(word, "guide")) {
    out = AppTarget::Guide;
    return true;
  }
  return false;
}

bool parseCwArgs(char* args, CwArgs& out) {
  out = CwArgs{};
  if (args == nullptr) return true;
  char* tok = nullptr;
  if (!nextToken(args, tok)) return true;  // CW: the dump
  if (tokenIs(tok, "open")) {
    // The rest of the line is the key (a card path may hold spaces).
    while (isSpace(*args)) ++args;
    size_t n = strlen(args);
    while (n > 0 && isSpace(args[n - 1])) --n;
    if (n == 0 || n > 96) return false;
    for (size_t i = 0; i < n; ++i) {
      const auto c = static_cast<unsigned char>(args[i]);
      if (c < 0x20 || c == 0x7F) return false;
    }
    memcpy(out.text, args, n);
    out.text[n] = '\0';
    out.op = CwOp::Open;
    return true;
  }
  if (tokenIs(tok, "type")) {
    char* letters = nullptr;
    if (!nextToken(args, letters) || nextToken(args, tok)) return false;
    const size_t n = strlen(letters);
    if (n == 0 || n > CW_TEXT_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
      const char c = letters[i];
      if (c == '-') {
        out.text[i] = c;
      } else if (toLower(c) >= 'a' && toLower(c) <= 'z') {
        out.text[i] = static_cast<char>(toLower(c) - 'a' + 'A');
      } else {
        return false;
      }
    }
    out.text[n] = '\0';
    out.op = CwOp::Type;
    return true;
  }
  if (tokenIs(tok, "cursor")) {
    uint32_t row = 0;
    uint32_t col = 0;
    char* r = nullptr;
    char* c = nullptr;
    if (!nextToken(args, r) || !nextToken(args, c) || !parseU32(r, row) || !parseU32(c, col)) return false;
    if (row >= static_cast<uint32_t>(CW_SIDE_MAX) || col >= static_cast<uint32_t>(CW_SIDE_MAX)) return false;
    out.row = static_cast<int8_t>(row);
    out.col = static_cast<int8_t>(col);
    if (nextToken(args, tok)) {
      if (tokenIs(tok, "a")) {
        out.dir = 0;
      } else if (tokenIs(tok, "d")) {
        out.dir = 1;
      } else {
        return false;
      }
      if (nextToken(args, tok)) return false;
    }
    out.op = CwOp::Cursor;
    return true;
  }
  if (tokenIs(tok, "check") || tokenIs(tok, "reveal")) {
    const CwOp op = tokenIs(tok, "check") ? CwOp::Check : CwOp::Reveal;
    char* scope = nullptr;
    if (!nextToken(args, scope) || nextToken(args, tok)) return false;
    static constexpr const char* SCOPES[] = {"letter", "word", "puzzle"};
    for (uint8_t i = 0; i < 3; ++i) {
      if (tokenIs(scope, SCOPES[i])) {
        out.op = op;
        out.scope = i;
        return true;
      }
    }
    return false;
  }
  CwOp op = CwOp::Dump;
  if (tokenIs(tok, "solve")) {
    op = CwOp::Solve;
  } else if (tokenIs(tok, "list")) {
    op = CwOp::List;
  } else {
    return false;
  }
  if (nextToken(args, tok)) return false;
  out.op = op;
  return true;
}

namespace {

bool suTier(const char* tok, int8_t& out) {
  static constexpr const char* TIERS[] = {"easy", "medium", "hard", "expert"};
  for (int8_t i = 0; i < 4; ++i) {
    if (tokenIs(tok, TIERS[i])) {
      out = i;
      return true;
    }
  }
  return false;
}

// A 1-9 token (a row, a column or a digit).
bool suOneToNine(const char* tok, int8_t& out) {
  uint32_t v = 0;
  if (!parseU32(tok, v) || v < 1 || v > 9) return false;
  out = static_cast<int8_t>(v);
  return true;
}

// A seed: decimal, or hex after 0x.
bool suSeed(const char* tok, uint32_t& out) {
  if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) return parseHex32(tok + 2, out);
  return parseU32(tok, out);
}

}  // namespace

bool parseSuArgs(char* args, SuArgs& out) {
  out = SuArgs{};
  if (args == nullptr) return true;
  char* tok = nullptr;
  if (!nextToken(args, tok)) return true;  // SU: the dump
  char* a = nullptr;
  char* b = nullptr;
  char* c = nullptr;
  if (tokenIs(tok, "new") || tokenIs(tok, "seed") || tokenIs(tok, "gen")) {
    const SuOp op = tokenIs(tok, "new") ? SuOp::New : tokenIs(tok, "seed") ? SuOp::Seed : SuOp::Gen;
    if (!nextToken(args, a) || !nextToken(args, b) || nextToken(args, c)) return false;
    if (!suTier(a, out.tier)) return false;
    if (op == SuOp::New ? (!parseU32(b, out.value) || out.value == 0) : !suSeed(b, out.value)) return false;
    out.op = op;
    return true;
  }
  if (tokenIs(tok, "put") || tokenIs(tok, "note") || tokenIs(tok, "erase")) {
    const SuOp op = tokenIs(tok, "put") ? SuOp::Put : tokenIs(tok, "note") ? SuOp::Note : SuOp::Erase;
    if (!nextToken(args, a) || !nextToken(args, b)) return false;
    if (!suOneToNine(a, out.row) || !suOneToNine(b, out.col)) return false;
    if (op != SuOp::Erase && (!nextToken(args, c) || !suOneToNine(c, out.digit))) return false;
    if (nextToken(args, c)) return false;
    out.row = static_cast<int8_t>(out.row - 1);
    out.col = static_cast<int8_t>(out.col - 1);
    out.op = op;
    return true;
  }
  if (tokenIs(tok, "check") || tokenIs(tok, "reveal")) {
    const SuOp op = tokenIs(tok, "check") ? SuOp::Check : SuOp::Reveal;
    if (nextToken(args, a)) {
      if (tokenIs(a, "square")) {
        out.scope = 0;
      } else if (!tokenIs(a, "puzzle")) {
        return false;
      }
      if (nextToken(args, b)) return false;
    }
    out.op = op;
    return true;
  }
  SuOp op = SuOp::Dump;
  if (tokenIs(tok, "hint")) {
    op = SuOp::Hint;
  } else if (tokenIs(tok, "solve")) {
    op = SuOp::Solve;
  } else {
    return false;
  }
  if (nextToken(args, a)) return false;
  out.op = op;
  return true;
}

namespace {

// [a-z0-9-]{1,maxLen}: a guide pack id (lib/Guide's validId, kept apart so this file stays alone).
bool gdId(const char* s, const size_t maxLen) {
  size_t n = 0;
  for (; s[n] != '\0'; ++n) {
    const char c = s[n];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return n >= 1 && n <= maxLen;
}

}  // namespace

bool parseGdArgs(char* args, GdArgs& out) {
  out = GdArgs{};
  if (args == nullptr) return true;
  char* tok = nullptr;
  if (!nextToken(args, tok)) return true;  // GD: the dump
  char* a = nullptr;
  char* b = nullptr;
  if (tokenIs(tok, "search")) {
    // The rest of the line as typed, its ends trimmed.
    while (isSpace(*args)) ++args;
    size_t n = std::strlen(args);
    while (n > 0 && isSpace(args[n - 1])) --n;
    if (n == 0 || n > GD_TEXT_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
      if (static_cast<unsigned char>(args[i]) < 0x20 || args[i] == 0x7F) return false;
    }
    std::memcpy(out.text, args, n);
    out.text[n] = '\0';
    out.op = GdOp::Search;
    return true;
  }
  if (tokenIs(tok, "open")) {
    if (!nextToken(args, a) || !gdId(a, 32)) return false;
    uint32_t page = 1;
    if (nextToken(args, b) && (!parseU32(b, page) || page < 1 || page > 64)) return false;
    if (nextToken(args, b)) return false;
    std::snprintf(out.text, sizeof(out.text), "%s", a);
    out.page = static_cast<uint8_t>(page - 1);
    out.op = GdOp::Open;
    return true;
  }
  if (tokenIs(tok, "list")) {
    if (nextToken(args, a)) {
      if (!gdId(a, 20) || nextToken(args, b)) return false;
      std::snprintf(out.text, sizeof(out.text), "%s", a);
    }
    out.op = GdOp::List;
    return true;
  }
  if (tokenIs(tok, "row")) {
    uint32_t row = 0;
    if (!nextToken(args, a) || !parseU32(a, row) || row > 1023 || nextToken(args, b)) return false;
    out.row = static_cast<uint16_t>(row);
    out.op = GdOp::Row;
    return true;
  }
  if (tokenIs(tok, "figure")) {
    if (nextToken(args, a)) {
      if (tokenIs(a, "close")) {
        out.close = true;
      } else if (!tokenIs(a, "open")) {
        return false;
      }
      if (nextToken(args, b)) return false;
    }
    out.op = GdOp::Figure;
    return true;
  }
  static constexpr struct {
    const char* word;
    GdOp op;
  } SIMPLE[] = {{"about", GdOp::About}, {"home", GdOp::Home}, {"next", GdOp::Next},
                {"prev", GdOp::Prev},   {"mark", GdOp::Mark}, {"menu", GdOp::Menu}};
  for (const auto& s : SIMPLE) {
    if (!tokenIs(tok, s.word)) continue;
    if (nextToken(args, a)) return false;
    out.op = s.op;
    return true;
  }
  return false;
}

bool parseWsArgs(char* args, WsArgs& out) {
  out = WsArgs{};
  if (args == nullptr) return true;
  char* tok = nullptr;
  if (!nextToken(args, tok)) return true;  // WS: the dump
  if (!tokenIs(tok, "new")) return false;
  out.newPuzzle = true;
  if (!nextToken(args, tok) || !parseU32(tok, out.seed)) return false;
  // An optional difficulty, then the rest of the line is the theme key.
  while (isSpace(*args)) ++args;
  char* key = args;
  size_t word = 0;
  while (key[word] != '\0' && !isSpace(key[word])) ++word;
  static constexpr const char* NAMES[] = {"easy", "medium", "hard"};
  for (int8_t d = 0; d < 3; ++d) {
    const char* name = NAMES[d];
    size_t i = 0;
    while (i < word && name[i] != '\0' && toLower(key[i]) == name[i]) ++i;
    if (i == word && name[i] == '\0') {
      out.difficulty = d;
      key += word;
      while (isSpace(*key)) ++key;
      break;
    }
  }
  size_t n = strlen(key);
  while (n > 0 && isSpace(key[n - 1])) key[--n] = '\0';
  if (n > WS_KEY_MAX) return false;
  for (size_t i = 0; i < n; ++i) {
    const auto c = static_cast<unsigned char>(key[i]);
    if (c < 0x20 || c == 0x7F) return false;
  }
  memcpy(out.themeKey, key, n);
  out.themeKey[n] = '\0';
  return true;
}

bool parsePinsArgs(const char* args, uint32_t& seconds) {
  seconds = PINS_DEFAULT_SECONDS;
  if (args == nullptr) return true;
  while (isSpace(*args)) ++args;
  size_t n = strlen(args);
  while (n > 0 && isSpace(args[n - 1])) --n;
  if (n == 0) return true;
  char number[12] = {};
  if (n >= sizeof(number)) return false;
  memcpy(number, args, n);
  uint32_t value = 0;
  if (!parseU32(number, value) || value < 1 || value > PINS_MAX_SECONDS) return false;
  seconds = value;
  return true;
}

bool isReservedS3Pin(const uint8_t pin) {
  return pin > PINS_MAX_GPIO || pin == 19 || pin == 20 || (pin >= 22 && pin <= 25) || (pin >= 26 && pin <= 37) ||
         pin == 43 || pin == 44;
}

bool ChangeBudget::allow(const uint32_t nowMs) {
  if (!started || nowMs - windowStart >= 1000) {
    started = true;
    windowStart = nowMs;
    used = 0;
  }
  if (used < limit) {
    ++used;
    return true;
  }
  ++droppedCount;
  return false;
}

}  // namespace bench
