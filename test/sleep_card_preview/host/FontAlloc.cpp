// Host implementation of FreeInkFont's allocator (plain heap, no PSRAM).
#include <FontAlloc.h>

#include <cstdlib>

extern "C" {
void* fiFontMalloc(size_t size) { return std::malloc(size); }
void* fiFontRealloc(void* ptr, size_t size) { return std::realloc(ptr, size); }
void fiFontFree(void* ptr) { std::free(ptr); }
}
