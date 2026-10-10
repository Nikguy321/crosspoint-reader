#pragma once

// Bench console wire protocol helpers: pure C++ (no Arduino) so the host test
// suite can exercise them. The firmware side lives in src/util/BenchConsole.
//
// Host -> device: "CMD:<VERB> <args>\n". Device -> host: lines prefixed "@B ",
// each command ending in exactly one "@B OK <VERB> ..." or "@B ERR <VERB> ...".

#include <cstddef>
#include <cstdint>

namespace bench {

constexpr int PROTOCOL_VERSION = 1;
constexpr size_t PUT_CHUNK_MAX = 4096;
constexpr size_t SHOT_BYTES_PER_LINE = 96;
constexpr size_t MAX_PATH_LEN = 240;  // leaves room for PART_SUFFIX under FAT's 255
inline constexpr char PART_SUFFIX[] = ".bench-part";

// zlib-compatible CRC-32 (same result as Python's zlib.crc32). Pass the value
// returned by the previous call to continue a running CRC; start from 0.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);

// Standard base64 with padding. Writes a NUL-terminated string and returns its
// length, or 0 when outCap cannot hold base64Length(len) + 1 bytes.
constexpr size_t base64Length(const size_t len) { return (len + 2) / 3 * 4; }
size_t base64Encode(const uint8_t* in, size_t len, char* out, size_t outCap);

// Splits "CMD:<VERB> rest" in place. On success `verb` points at the upper-cased
// verb and `rest` at the argument text (leading spaces skipped, never null).
// Returns false when the line is not a CMD: line.
bool splitCommand(char* line, char*& verb, char*& rest);

// Pops the next space-separated token from `cursor` in place (NUL-terminates it
// and advances past trailing spaces). Returns false when no token remains.
bool nextToken(char*& cursor, char*& token);

// Strict decimal / hex parsers: the whole string must be consumed.
bool parseU32(const char* s, uint32_t& out);
bool parseI32(const char* s, int32_t& out);
bool parseHex32(const char* s, uint32_t& out);

// 32 hex digits. Lower-cases the string in place so it compares with MD5Builder.
bool normalizeMd5Hex(char* s);

// An absolute SD path the console may name: starts with '/', no empty, "." or
// ".." components, no control characters, backslashes or FAT-reserved
// characters (" * : < > ? |), at most MAX_PATH_LEN bytes. The root "/" is valid
// only when allowRoot is set.
bool isValidPath(const char* path, bool allowRoot);

// True when the path ends with PART_SUFFIX, ignoring ASCII case as FAT does
// (the console's own temporary files).
bool isPartPath(const char* path);

// Removes trailing '/' characters (keeping a lone "/").
void trimTrailingSlashes(char* path);

// Parses a PUT chunk header "<len> <crc32hex>". len must be 1..maxLen.
bool parseChunkHeader(const char* line, uint32_t maxLen, uint32_t& len, uint32_t& crc);

// CAT [<tailBytes>] <path>: the last tailBytes of a text file, one "L <line>"
// reply per line (a partial first line is dropped when the window starts
// mid-file), then "OK CAT lines= bytes= size=". Parses the arguments in place:
// a leading integer followed by a space is the window (clamped to 1..CAT_TAIL_MAX,
// CAT_TAIL_DEFAULT when absent); everything after it is the path, which the
// caller still validates. Returns false when no path remains.
constexpr uint32_t CAT_TAIL_DEFAULT = 4096;
constexpr uint32_t CAT_TAIL_MAX = 16384;
constexpr size_t CAT_LINE_CAP = 200;  // longer lines are truncated on the wire
bool parseCatArgs(char* args, uint32_t& tailBytes, char*& path);

// SLEEP [deep]. Default: the sleep a power press gives (on external power, the live sleep screen
// when it can be live; on that screen, a redraw). Deep: deep sleep even on external power, and
// from the live screen its commit to deep sleep. False for anything else.
enum class SleepKind : uint8_t { Default, Deep };
bool parseSleepArgs(const char* args, SleepKind& out);

// WIFILAST <ssid>: the rest of the line is the SSID, as the Wi-Fi list stores it: 1..32 bytes,
// no control characters (spaces are part of an SSID).
constexpr size_t SSID_MAX_BYTES = 32;
bool isValidSsid(const char* ssid);

// APP apps|wordsearch|crossword|sudoku|guide: open the Apps list or a game / the guide (case ignored).
enum class AppTarget : uint8_t { Apps, WordSearch, Crossword, Sudoku, Guide };
bool parseAppArgs(const char* args, AppTarget& out);

// WEATHER [show|fetch|clear]: the Weather card's cache summarised (the default), a fetch now on a
// station already up, or the cache and its retry stamp removed (case ignored).
enum class WeatherOp : uint8_t { Show, Fetch, Clear };
bool parseWeatherArgs(const char* args, WeatherOp& out);

// POWER [fake absent|fake real]: external power as live sleep reads it (the default), or a fake:
// "fake absent" makes it read absent whatever the charger line and this cable say, so the unplug
// and the full-charge hold can be tried on the bench; "fake real" (or "real") ends the fake.
// Case ignored.
enum class PowerOp : uint8_t { Show, FakeAbsent, Real };
bool parsePowerArgs(const char* args, PowerOp& out);

// LOCPHONE [off | <a.b.c.d>[:<port>]]: LOCTEST's phone source (network/PhoneGps) read from this
// address instead of the network's gateway - a computer on the same network stands in for the
// phone - or the gateway again (off), or the setting shown (no argument). RAM only; Locate Me and
// AutoLocate never use it. The address is
// four decimal octets, not 0.0.0.0 or 255.255.255.255; the port 1..65535 (none: 10110, then
// 11123). Case ignored for "off".
enum class LocPhoneOp : uint8_t { Show, Off, Set };
struct LocPhoneArgs {
  LocPhoneOp op = LocPhoneOp::Show;
  uint8_t ip[4] = {};
  uint16_t port = 0;
};
bool parseLocPhoneArgs(const char* args, LocPhoneArgs& out);

// LOCTEST [coords]: the locate pipeline (network/LocateRun) on the station already up, its verdict
// printed and nothing saved; "coords" adds the position at two decimals. Case ignored.
bool parseLocTestArgs(const char* args, bool& coords);

// WS                                         the puzzle on screen, dumped
// WS new <seed> [easy|medium|hard] [key]     a deterministic new puzzle; the key (a built-in
//                                            theme or "file:<name>.words") is the rest of the
//                                            line, so a file name may hold spaces
// difficulty -1 = the player's choice; an empty key = a built-in theme the seed picks (never the
// choice, the card's files or the recent list, so the puzzle stays reproducible). Parses in place.
constexpr size_t WS_KEY_MAX = 47;  // ws::MAX_THEME_KEY
struct WsArgs {
  bool newPuzzle = false;
  uint32_t seed = 0;
  int8_t difficulty = -1;
  char themeKey[WS_KEY_MAX + 1] = {};
};
bool parseWsArgs(char* args, WsArgs& out);

// CW                                  the crossword on screen, dumped
// CW open builtin:<id>|<path>          open a puzzle (the key is the rest of the line)
// CW type <letters>                    A-Z (case ignored) through the keyboard's handler; '-' = Del
// CW cursor <row> <col> [A|D]          the cursor on a white square (0-based)
// CW check|reveal letter|word|puzzle
// CW solve                             types the answers in (completion tests)
// CW list                              the built-in puzzles
// Parses in place.
enum class CwOp : uint8_t { Dump, Open, Type, Cursor, Check, Reveal, Solve, List };
constexpr size_t CW_TEXT_MAX = 120;  // a key (cw::MAX_SOURCE_KEY 96) or the letters typed at once
constexpr int CW_SIDE_MAX = 15;      // cw::MAX_SIDE
struct CwArgs {
  CwOp op = CwOp::Dump;
  char text[CW_TEXT_MAX + 1] = {};  // Open: the key; Type: the letters, upper case
  int8_t row = -1;
  int8_t col = -1;
  int8_t dir = -1;    // Cursor: 0 Across, 1 Down, -1 keep
  uint8_t scope = 0;  // Check / Reveal: 0 letter, 1 word, 2 puzzle (cw::Scope)
};
bool parseCwArgs(char* args, CwArgs& out);

// SU                              the sudoku on screen, dumped
// SU new <tier> <n>                numbered puzzle n of a tier, as New puzzle makes it (the prefs'
//                                  counters are left alone)
// SU seed <tier> <seed>            a puzzle from a raw seed (decimal, or hex with 0x), number 0
// SU put <r> <c> <d>               a digit (rows, columns 1-9; digits 1-9) through the model
// SU note <r> <c> <d>              toggles a pencil mark
// SU erase <r> <c>                 the square's digit, else its notes
// SU hint | SU solve               a hint as the menu row; every missing or wrong digit written
// SU check|reveal [square|puzzle]  as the menu rows (default puzzle)
// SU gen <tier> <seed>             generates off-screen (on any screen): the time and the clock
// Tiers: easy medium hard expert (case ignored). Parses in place.
enum class SuOp : uint8_t { Dump, New, Seed, Put, Note, Erase, Hint, Check, Reveal, Solve, Gen };
struct SuArgs {
  SuOp op = SuOp::Dump;
  int8_t tier = -1;    // New, Seed, Gen: 0 easy .. 3 expert
  uint32_t value = 0;  // New: the number (>= 1); Seed, Gen: the seed
  int8_t row = -1;     // Put, Note, Erase: 0..8 (the wire's 1..9 less one)
  int8_t col = -1;
  int8_t digit = 0;   // Put, Note: 1..9
  uint8_t scope = 1;  // Check, Reveal: 0 square, 1 puzzle (sd::Scope)
};
bool parseSuArgs(char* args, SuArgs& out);

// GD                       the guide screen on screen (and the pack), dumped
// GD open <topic> [page]   a topic's page (1-based, default 1), as its list row opens it
// GD about | GD home       About & sources | the guide home
// GD search <words>        the results of a search (the rest of the line, 1..63 bytes)
// GD list [category]       the pack's categories, or one category's topics (no screen change)
// GD next | GD prev        a page turn (pages) or the selection (lists), as the right / left key
// GD mark                  the open page's bookmark toggled (as the right key held)
// GD menu                  up a level (the page's MENU, a list's BACK)
// GD row <n>               opens list row n (0-based, as a tap on it)
// GD figure [close]        the page's figure full screen (or closed)
// Ids are [a-z0-9-] (topics up to 32 bytes, categories 20); words other than the search's are
// matched without case. Parses in place.
enum class GdOp : uint8_t { Dump, Open, About, Home, Search, List, Next, Prev, Mark, Menu, Row, Figure };
constexpr size_t GD_TEXT_MAX = 63;
struct GdArgs {
  GdOp op = GdOp::Dump;
  char text[GD_TEXT_MAX + 1] = {};  // Open: the topic id; Search: the words; List: the category ("" all)
  uint8_t page = 0;                 // Open: 0-based (the wire's 1-based less one)
  uint16_t row = 0;                 // Row
  bool close = false;               // Figure close
};
bool parseGdArgs(char* args, GdArgs& out);

// PINS [seconds]: the USB/VBUS-detect pin hunt (default 60, 1..180).
constexpr uint32_t PINS_DEFAULT_SECONDS = 60;
constexpr uint32_t PINS_MAX_SECONDS = 180;
bool parsePinsArgs(const char* args, uint32_t& seconds);

// X4 Pro pins for PINS. ASSIGNED is every pin the X4 Pro profile uses (freeink-sdk BoardConfig.h,
// XTEINK_X4_PRO): display SCLK 12 MOSI 11 CS 13 DC 18 RST 14 BUSY 6; SD (SDMMC CLK 41 CMD 42
// DAT0 40, the SPI view's CS 45, enable 5); keys up 0, down 7, power 3; charger STAT 21; GT911
// SDA 39 SCL 38 INT 10 RST 4 power 2 (the RTC and the gauge share 38/39); frontlight 8 and 9;
// the power-rail latch 1. The ESP32-S3 pins nothing may touch: 19/20 (USB D-/D+), 26-37 (the
// flash and the octal PSRAM), 43/44 (UART0), and 22-25 do not exist.
//
// PROBE is what remains, input-enabled only (no pull, no direction or function change):
//   15, 16  XTAL_32K_P/N: the firmware runs its RTC on the internal RC
//           (CONFIG_RTC_CLK_SRC_INT_RC in the X4 Pro's dio_opi sdkconfig), so no 32 kHz crystal
//           is driven from them; no BoardConfig field or HAL file names them.
//   17      no BoardConfig field, HAL file (lib/hal) or SDK driver names it.
//   46      a strap (ROM log / boot mode) read only at reset; nothing names it after boot.
//   47, 48  no BoardConfig field, HAL file or SDK driver names them.
// None is on ADC1 (GPIO1-10, all assigned), so the per-second line reads no ADC.
// The device re-checks every probe pin against the live profile before touching it.
constexpr uint8_t X4PRO_ASSIGNED_PINS[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                           12, 13, 14, 18, 21, 38, 39, 40, 41, 42, 45};
constexpr uint8_t X4PRO_PROBE_PINS[] = {15, 16, 17, 46, 47, 48};
// Watched as well (read from the input registers, never reconfigured): the two charge/USB
// suspects the profile already uses (GT911 INT 10, the SDK's usbDetect guess; STAT 21) and the
// keys (a known level change for comparison). Bus, PWM and refresh pins are left out: they
// toggle on their own.
constexpr uint8_t X4PRO_WATCH_ASSIGNED_PINS[] = {0, 3, 7, 10, 21};
constexpr uint8_t PINS_MAX_GPIO = 48;
// A pin the chip reserves (USB, flash/PSRAM, UART0) or that does not exist.
bool isReservedS3Pin(uint8_t pin);

// At most `perSecond` pin-change lines in any one-second window; the rest are counted.
class ChangeBudget {
 public:
  explicit ChangeBudget(uint16_t perSecond) : limit(perSecond) {}
  bool allow(uint32_t nowMs);
  uint32_t dropped() const { return droppedCount; }

 private:
  uint16_t limit;
  uint16_t used = 0;
  uint32_t windowStart = 0;
  bool started = false;
  uint32_t droppedCount = 0;
};

// Bookkeeping for one PUT receive: which bytes to read next, when to ACK, NAK
// or abort. The firmware does the serial and card I/O around it.
class PutSession {
 public:
  static constexpr int MAX_NAKS = 5;
  enum class Action : uint8_t { ReadPayload, Write, Nak, Abort };

  explicit PutSession(uint32_t size) : expected(size) {}
  // A header line arrived; lineOk is false when it overflowed the line buffer.
  // ReadPayload: read chunkLen() bytes. Nak / Abort: see reason().
  Action onHeader(const char* line, bool lineOk);
  // The payload read after ReadPayload: `got` bytes of chunkLen() arrived.
  // Write: store chunkLen() bytes, then call committed().
  Action onPayload(const uint8_t* data, size_t got);
  // The chunk was written: it counts, and the NAK streak ends.
  void committed();

  bool complete() const { return received >= expected; }
  uint32_t total() const { return received; }
  uint32_t chunkLen() const { return len; }
  const char* reason() const { return why; }

 private:
  Action nak(const char* reason);

  uint32_t expected;
  uint32_t received = 0;
  uint32_t len = 0;
  uint32_t crc = 0;
  int naks = 0;
  const char* why = "";
};

// Optional PING nonce: 1-16 characters of [A-Za-z0-9_-], echoed so the host can
// tell its reply from stale output of an earlier session.
bool isValidNonce(const char* s);

// Named keys. Button keys are HalGPIO indices 0-6; Home keys drive the
// capacitive Home key through the touch controller's key events.
enum class KeyKind : uint8_t { Button, Home };
struct KeyDef {
  const char* name;
  KeyKind kind;
  uint8_t buttonIndex;  // HalGPIO::BTN_* for KeyKind::Button
  uint16_t defaultHoldMs;
  uint8_t presses;  // 2 for the double-press names
};
bool lookupKey(const char* name, KeyDef& out);

// Double-press gestures (KEY powerdouble / homedouble): each press held this
// long, released for DOUBLE_GAP_MS between them. Both fit the firmware's
// double-click windows (power 500 ms, Home 350 ms between taps).
constexpr uint16_t DOUBLE_HOLD_MS = 60;
constexpr uint16_t DOUBLE_GAP_MS = 100;

constexpr uint32_t HOLD_MIN_MS = 20;
constexpr uint32_t HOLD_MAX_MS = 10000;
uint32_t clampHoldMs(uint32_t ms);

}  // namespace bench
