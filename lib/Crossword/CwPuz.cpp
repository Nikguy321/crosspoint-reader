#include "CwPuz.h"

#include <cstdio>
#include <cstring>

#include "CwNumbering.h"
#include "CwText.h"

namespace cw {

namespace {

constexpr size_t HEADER = 0x34;
constexpr char MAGIC[] = "ACROSS&DOWN";  // with its NUL: 12 bytes at 0x02
constexpr uint16_t TYPE_DIAGRAMLESS = 0x0401;

uint16_t le16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }

uint16_t step(uint16_t sum, const uint8_t b) {
  sum = (sum & 1) ? static_cast<uint16_t>((sum >> 1) + 0x8000) : static_cast<uint16_t>(sum >> 1);
  return static_cast<uint16_t>(sum + b);
}

// The text checksums run twice: on from the grids (global) and from 0 (the masked "part").
struct TextSums {
  uint16_t global = 0;
  uint16_t part = 0;
};

// Reads a NUL-terminated string, feeding it to the cleaner (when given) and the checksums.
// withNul: a non-empty string's NUL is summed too (title, author, copyright, notes). False at
// the end of the file before the NUL.
bool readString(ByteReader& in, TextCleaner* out, TextSums* sums, const bool withNul) {
  TextSums tmp = sums ? *sums : TextSums{};
  size_t n = 0;
  while (true) {
    const int b = in.read();
    if (b < 0) return false;
    if (b == 0) break;
    n++;
    if (out) out->put(static_cast<char>(b));
    tmp.global = step(tmp.global, static_cast<uint8_t>(b));
    tmp.part = step(tmp.part, static_cast<uint8_t>(b));
  }
  if (sums && n > 0) {
    if (withNul) {
      tmp.global = step(tmp.global, 0);
      tmp.part = step(tmp.part, 0);
    }
    *sums = tmp;
  }
  return true;
}

// The entry the k-th .puz clue belongs to: numbered squares in reading order, Across first.
struct ClueOrder {
  int cell = 0;
  int phase = 0;  // 0: Across at this cell next, 1: Down
  int next(const Puzzle& p) {
    for (; cell < p.cells(); cell++, phase = 0) {
      if (p.number[cell] == 0) continue;
      for (; phase < 2; phase++) {
        const uint8_t e = p.entryAt[phase][cell];
        if (e != NO_ENTRY && p.entryCell(e, 0) == cell) {
          phase++;
          return e;
        }
      }
    }
    return -1;
  }
};

}  // namespace

uint16_t puzChecksum(const uint8_t* data, const size_t len, uint16_t sum) {
  for (size_t i = 0; i < len; i++) sum = step(sum, data[i]);
  return sum;
}

LoadStatus parsePuz(ByteReader& in, const char* sourceKey, Puzzle& p) {
  p.reset();
  LoadStatus st;
  const auto fail = [&st, &p](const Error e) {
    p.reset();
    st.error = e;
    return st;
  };
  uint8_t hdr[HEADER];
  const size_t got = in.readBytes(reinterpret_cast<char*>(hdr), HEADER);
  if (got < 0x0E || std::memcmp(hdr + 2, MAGIC, sizeof(MAGIC)) != 0) return fail(Error::NotCrossword);
  if (got < HEADER) return fail(Error::Damaged);
  const int w = hdr[0x2C];
  const int h = hdr[0x2D];
  st.width = static_cast<uint16_t>(w);
  st.height = static_cast<uint16_t>(h);
  if (w > MAX_SIDE || h > MAX_SIDE) return fail(Error::TooBig);
  if (w < MIN_SIDE || h < MIN_SIDE) return fail(Error::TooSmall);
  if (le16(hdr + 0x30) == TYPE_DIAGRAMLESS) return fail(Error::Diagramless);
  if (le16(hdr + 0x32) != 0) return fail(Error::Locked);
  const int clueCount = le16(hdr + 0x2E);
  const int major = (hdr[0x18] >= '0' && hdr[0x18] <= '9') ? hdr[0x18] - '0' : 1;
  const int minor = (hdr[0x19] == '.' && hdr[0x1A] >= '0' && hdr[0x1A] <= '9') ? hdr[0x1A] - '0' : 0;
  const uint8_t textFlags = major >= 2 ? 0 : TEXT_LATIN1;
  const bool notesSummed = major > 1 || (major == 1 && minor >= 3);
  p.w = static_cast<uint8_t>(w);
  p.h = static_cast<uint8_t>(h);
  const int cells = w * h;

  // The solution, then the player grid (summed only). global runs on from the CIB sum.
  const uint16_t cib = puzChecksum(hdr + 0x2C, 8, 0);
  uint16_t global = cib;
  uint16_t solSum = 0;
  bool rebus = false;
  for (int i = 0; i < cells; i++) {
    const int b = in.read();
    if (b < 0) return fail(Error::Damaged);
    solSum = step(solSum, static_cast<uint8_t>(b));
    global = step(global, static_cast<uint8_t>(b));
    if (b == '.') {
      p.solution[i] = BLOCK;
    } else if (b >= 'a' && b <= 'z') {
      p.solution[i] = static_cast<char>(b - 'a' + 'A');
    } else if (b >= 'A' && b <= 'Z') {
      p.solution[i] = static_cast<char>(b);
    } else {
      p.solution[i] = 'A';
      rebus = true;
    }
  }
  uint16_t gridSum = 0;
  for (int i = 0; i < cells; i++) {
    const int b = in.read();
    if (b < 0) return fail(Error::Damaged);
    gridSum = step(gridSum, static_cast<uint8_t>(b));
    global = step(global, static_cast<uint8_t>(b));
  }
  if (rebus) return fail(Error::Rebus);
  const Error numbered = numberGrid(p);
  if (numbered != Error::None) return fail(numbered);
  if (clueCount != p.entryCount) return fail(Error::Damaged);

  // The strings: title, author, copyright, the clues, notes.
  TextSums sums;
  sums.global = global;
  char* const fields[3] = {p.title, p.author, p.copyright};
  const size_t caps[3] = {sizeof(p.title), sizeof(p.author), sizeof(p.copyright)};
  for (int f = 0; f < 3; f++) {
    TextCleaner tc(fields[f], caps[f], textFlags);
    if (!readString(in, &tc, &sums, true)) return fail(Error::Damaged);
    tc.finish();
  }
  ClueOrder order;
  for (int k = 0; k < clueCount; k++) {
    const int e = order.next(p);
    TextCleaner tc(nullptr, 0, textFlags);
    if (e < 0) return fail(Error::Damaged);
    if (!beginClue(p, tc, textFlags)) return fail(Error::TooManyClues);
    if (!readString(in, &tc, &sums, false)) return fail(Error::Damaged);
    if (tc.truncated()) st.truncatedClues++;
    commitClue(p, e, tc);
  }
  // Notes may be missing at the very end of a file; everything before them is complete.
  TextSums withNotes = sums;
  const bool haveNotes = readString(in, nullptr, &withNotes, true);
  if (haveNotes && notesSummed) sums = withNotes;

  // Extensions: <code:4> <len:2> <sum:2> <data:len> <NUL>. Real files carry short trailers (a
  // CR LF, a few NULs) and cut extensions: the grid and clues are complete by now, so only a cut
  // GRBS (a rebus that might be missed) refuses; the rest ends the data with a warning.
  while (haveNotes) {
    uint8_t ext[8];
    const size_t n = in.readBytes(reinterpret_cast<char*>(ext), sizeof(ext));
    if (n == 0) break;
    if (n < sizeof(ext)) {
      st.checksumWarnings++;
      break;
    }
    const uint16_t len = le16(ext + 4);
    const bool gext = std::memcmp(ext, "GEXT", 4) == 0;
    const bool grbs = std::memcmp(ext, "GRBS", 4) == 0;
    uint16_t sum = 0;
    bool cut = false;
    for (int i = 0; i < len; i++) {
      const int b = in.read();
      if (b < 0) {
        cut = true;
        break;
      }
      sum = step(sum, static_cast<uint8_t>(b));
      if (gext && len == cells && (b & 0x80) && !p.isBlock(i)) p.setCircled(i);
      if (grbs && b != 0) rebus = true;
    }
    if (rebus) return fail(Error::Rebus);
    if (cut) {
      if (grbs) return fail(Error::Damaged);
      st.checksumWarnings++;
      break;
    }
    if (sum != le16(ext + 6)) st.checksumWarnings++;
    if (in.read() < 0) break;  // the trailing NUL; tolerated missing at the end
  }

  // Checksums: counted, never fatal.
  if (cib != le16(hdr + 0x0E)) st.checksumWarnings++;
  if (sums.global != le16(hdr + 0x00)) st.checksumWarnings++;
  const uint16_t parts[4] = {cib, solSum, gridSum, sums.part};
  const char lowKey[] = "ICHE";
  const char highKey[] = "ATED";
  bool masked = true;
  for (int i = 0; i < 4; i++) {
    masked = masked && hdr[0x10 + i] == static_cast<uint8_t>(lowKey[i] ^ (parts[i] & 0xFF));
    masked = masked && hdr[0x14 + i] == static_cast<uint8_t>(highKey[i] ^ (parts[i] >> 8));
  }
  if (!masked) st.checksumWarnings++;

  std::snprintf(p.sourceKey, sizeof(p.sourceKey), "%s", sourceKey ? sourceKey : "");
  return st;
}

}  // namespace cw
