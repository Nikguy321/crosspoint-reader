#pragma once

// Host stand-in for the SDK memory manager: sinks are accepted and never asked.
#include <cstddef>
#include <cstdint>
#include <functional>

namespace freeink {

enum class MemPool : unsigned char { Internal, Psram, Default };

struct CacheSink {
  const char* name = nullptr;
  uint8_t priority = 128;
  std::function<size_t(size_t)> release;
};

class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager inst;
    return inst;
  }
  int registerSink(const CacheSink&) { return 0; }
  size_t freeBytes(MemPool = MemPool::Default) const { return 256 * 1024; }
  bool ensureFree(size_t, MemPool = MemPool::Default) { return true; }

 private:
  MemoryManager() = default;
};

}  // namespace freeink
