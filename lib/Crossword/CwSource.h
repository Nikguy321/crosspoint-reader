#pragma once

// Crossword sources on the card: the folder layout, the caps, and the byte source the .ipuz and
// .puz parsers read from.
//
// /Puzzles/Crossword/            root files = the collection "On the card"
// /Puzzles/Crossword/<pack>/     each direct subfolder = a pack named after the folder
// Only *.ipuz and *.puz (any case) count; dot files and dot folders are ignored; one level only.
//
// ByteReader: the activity implements it over a HalFile (with its own small buffer: the .puz
// parser calls read() once per byte); the host tests feed memory. ArduinoJson reads it
// directly (it accepts any class with read() and readBytes()).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CwModel.h"

namespace cw {

inline constexpr char CARD_DIR[] = "/Puzzles/Crossword";
constexpr int MAX_PACKS = 32;
constexpr int MAX_FOLDER_FILES = 200;  // a listing stops here and says so
constexpr size_t MAX_IPUZ_BYTES = 128 * 1024;
constexpr size_t MAX_PUZ_BYTES = 64 * 1024;

enum class Format : uint8_t { None = 0, Ipuz, Puz };
// By extension (case-insensitive); None for anything else and for dot files.
Format formatOf(const char* fileName);
// The name without its extension into out (for the picker). Returns the length.
size_t displayName(const char* fileName, char* out, size_t cap);
// A folder that may hold a pack: not a dot folder, not empty.
bool isPackFolder(const char* name);

class ByteReader {
 public:
  virtual ~ByteReader() = default;
  // The next byte (0..255), or -1 at the end.
  virtual int read() = 0;
  // Up to n bytes into buf; fewer only at the end.
  virtual size_t readBytes(char* buf, size_t n) = 0;
};

class MemoryReader final : public ByteReader {
 public:
  MemoryReader(const void* data, const size_t len) : data(static_cast<const unsigned char*>(data)), len(len) {}
  int read() override { return pos < len ? data[pos++] : -1; }
  size_t readBytes(char* buf, const size_t n) override {
    const size_t k = n < len - pos ? n : len - pos;
    if (k > 0) std::memcpy(buf, data + pos, k);
    pos += k;
    return k;
  }
  size_t position() const { return pos; }

 private:
  const unsigned char* data;
  size_t len;
  size_t pos = 0;
};

// The heap the ipuz parser's JSON document uses (the device passes PSRAM functions). A nullptr
// Allocator* = malloc/realloc/free.
struct Allocator {
  void* (*allocate)(size_t size);
  void (*deallocate)(void* ptr);
  void* (*reallocate)(void* ptr, size_t newSize);
};

// The next unsolved puzzle after current in a collection of count (wrapping, current itself
// last); -1 when every one is solved ("All solved here"). current may be -1 (start at 0).
using IsSolved = bool (*)(void* ctx, int index);
int nextUnsolved(int count, int current, IsSolved solved, void* ctx);

// The outcome of loading a card file.
struct LoadStatus {
  Error error = Error::None;
  uint16_t width = 0;  // as the file gives them (for "17x17 - up to 15x15")
  uint16_t height = 0;
  uint16_t truncatedClues = 0;    // clues cut to MAX_CLUE_BYTES
  uint16_t checksumWarnings = 0;  // .puz checksums that did not match (never a rejection)
  bool ok() const { return error == Error::None; }
};

}  // namespace cw
