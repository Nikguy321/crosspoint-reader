#pragma once

// Host stand-in: HalFile over a host FILE*, enough for Bitmap (read/seek) and
// the preview's fake CardIo.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::FILE* f) : f_(f) {}
  HalFile(HalFile&& other) noexcept : f_(other.f_) { other.f_ = nullptr; }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      f_ = other.f_;
      other.f_ = nullptr;
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  ~HalFile() { close(); }

  explicit operator bool() const { return f_ != nullptr; }
  bool isOpen() const { return f_ != nullptr; }
  bool close() {
    if (f_) std::fclose(f_);
    f_ = nullptr;
    return true;
  }
  int read(void* buf, size_t count) { return f_ ? static_cast<int>(std::fread(buf, 1, count, f_)) : -1; }
  int read() {
    if (!f_) return -1;
    const int c = std::fgetc(f_);
    return c == EOF ? -1 : c;
  }
  bool seek(size_t pos) { return f_ && std::fseek(f_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seekSet(size_t pos) { return seek(pos); }
  bool seekCur(int64_t offset) { return f_ && std::fseek(f_, static_cast<long>(offset), SEEK_CUR) == 0; }
  size_t size() {
    if (!f_) return 0;
    const long here = std::ftell(f_);
    std::fseek(f_, 0, SEEK_END);
    const long end = std::ftell(f_);
    std::fseek(f_, here, SEEK_SET);
    return end < 0 ? 0 : static_cast<size_t>(end);
  }
  size_t fileSize() { return size(); }
  size_t position() const { return f_ ? static_cast<size_t>(std::ftell(f_)) : 0; }
  int available() const { return 0; }
  size_t write(const void* buf, size_t count) { return f_ ? std::fwrite(buf, 1, count, f_) : 0; }

 private:
  std::FILE* f_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file = HalFile(std::fopen(path, "rb"));
    return static_cast<bool>(file);
  }
  bool openFileForRead(const char* m, const std::string& path, HalFile& file) {
    return openFileForRead(m, path.c_str(), file);
  }
};

#define Storage HalStorage::getInstance()
