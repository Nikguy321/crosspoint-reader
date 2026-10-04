#include "CwSource.h"

namespace cw {

namespace {

// name ends with ext (".ipuz"), ignoring ASCII case.
bool endsWithNoCase(const char* name, const size_t len, const char* ext) {
  const size_t n = std::strlen(ext);
  if (len <= n) return false;
  for (size_t i = 0; i < n; i++) {
    char c = name[len - n + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != ext[i]) return false;
  }
  return true;
}

}  // namespace

Format formatOf(const char* fileName) {
  if (!fileName || fileName[0] == '\0' || fileName[0] == '.') return Format::None;
  const size_t len = std::strlen(fileName);
  if (endsWithNoCase(fileName, len, ".ipuz")) return Format::Ipuz;
  if (endsWithNoCase(fileName, len, ".puz")) return Format::Puz;
  return Format::None;
}

size_t displayName(const char* fileName, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  out[0] = '\0';
  if (!fileName) return 0;
  size_t len = std::strlen(fileName);
  const char* dot = std::strrchr(fileName, '.');
  if (dot && dot != fileName) len = static_cast<size_t>(dot - fileName);
  if (len >= cap) {
    // Cut on a UTF-8 code point.
    len = cap - 1;
    while (len > 0 && (static_cast<unsigned char>(fileName[len]) & 0xC0) == 0x80) len--;
  }
  std::memcpy(out, fileName, len);
  out[len] = '\0';
  return len;
}

bool isPackFolder(const char* name) { return name && name[0] != '\0' && name[0] != '.'; }

int nextUnsolved(const int count, const int current, const IsSolved solved, void* ctx) {
  if (count <= 0 || !solved) return -1;
  const int start = current < 0 || current >= count ? count - 1 : current;
  for (int k = 1; k <= count; k++) {
    const int i = (start + k) % count;
    if (!solved(ctx, i)) return i;
  }
  return -1;
}

}  // namespace cw
