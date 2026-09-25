#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Landing a percentage-only sync record (a WiPhone or COVEY sends `progress`
// "") on the TARGET chapter's own fraction. Pure math and the record format;
// the file I/O lives in src/util/BookSyncHooks.
namespace BookSync {

// Cumulative inflated size of the book through spine item `index`
// (Epub::getCumulativeSpineItemSize), read through a callback so this builds on
// the host.
using CumulativeSizeFn = size_t (*)(const void* ctx, int index);

struct SpinePoint {
  int spineIndex = 0;
  double within = 0.0;  // fraction of the way through that spine item, [0, 1]
};

// Inverse of CrossPoint's byte-weighted book percentage, as
// wiphone/tools/gen_kosync_vectors.py pins it: target = p * total; the first
// item whose cumulative size reaches the target (an exact boundary picks the
// earlier item); within = (target - start) / size, 0 for a zero-size item.
// nullopt when the book has no bytes, which is not syncable.
std::optional<SpinePoint> locateByteWeighted(double percentage, int spineCount, CumulativeSizeFn cumAt,
                                             const void* ctx);

// A point within JSON precision (6 decimals, plus a byte) of the end of its item
// is the start of the next non-empty item: the same byte, read as a chapter
// start rather than the previous chapter's last page.
SpinePoint snapToChapterStart(SpinePoint point, int spineCount, CumulativeSizeFn cumAt, const void* ctx);

// Carried across the post-sync reboot in the book's cache dir: magic "BSL1",
// spine u16, spine count u16, within in millionths u32, little-endian.
constexpr size_t LANDING_RECORD_SIZE = 12;
void encodeLanding(const SpinePoint& point, int spineCount, uint8_t (&out)[LANDING_RECORD_SIZE]);
// Rejects a wrong length or magic, a record written for another spine count, and
// an out-of-range spine or within.
std::optional<SpinePoint> decodeLanding(const uint8_t* data, size_t length, int spineCount);

}  // namespace BookSync
