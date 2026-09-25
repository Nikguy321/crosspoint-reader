#include "BookSyncLanding.h"

#include <algorithm>
#include <iterator>

namespace BookSync {

namespace {
constexpr uint8_t LANDING_MAGIC[4] = {'B', 'S', 'L', '1'};
constexpr uint32_t WITHIN_SCALE = 1000000;

double itemStart(const int index, const CumulativeSizeFn cumAt, const void* ctx) {
  return index > 0 ? static_cast<double>(cumAt(ctx, index - 1)) : 0.0;
}

void putLe(uint8_t* out, const uint32_t value, const int bytes) {
  for (int i = 0; i < bytes; i++) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t getLe(const uint8_t* in, const int bytes) {
  uint32_t value = 0;
  for (int i = 0; i < bytes; i++) value |= static_cast<uint32_t>(in[i]) << (8 * i);
  return value;
}
}  // namespace

std::optional<SpinePoint> locateByteWeighted(const double percentage, const int spineCount,
                                             const CumulativeSizeFn cumAt, const void* ctx) {
  if (spineCount <= 0) return std::nullopt;
  const double total = static_cast<double>(cumAt(ctx, spineCount - 1));
  if (total <= 0.0) return std::nullopt;

  const double p = std::clamp(percentage, 0.0, 1.0);
  const double target = p * total;
  int index = spineCount - 1;
  for (int i = 0; i < spineCount; i++) {
    if (static_cast<double>(cumAt(ctx, i)) >= target) {
      index = i;
      break;
    }
  }
  const double start = itemStart(index, cumAt, ctx);
  const double size = static_cast<double>(cumAt(ctx, index)) - start;
  SpinePoint point;
  point.spineIndex = index;
  point.within = size <= 0.0 ? 0.0 : std::clamp((target - start) / size, 0.0, 1.0);
  return point;
}

SpinePoint snapToChapterStart(const SpinePoint point, const int spineCount, const CumulativeSizeFn cumAt,
                              const void* ctx) {
  if (spineCount <= 0 || point.spineIndex < 0 || point.spineIndex >= spineCount) return point;
  const double total = static_cast<double>(cumAt(ctx, spineCount - 1));
  const double start = itemStart(point.spineIndex, cumAt, ctx);
  const double size = static_cast<double>(cumAt(ctx, point.spineIndex)) - start;
  const double bytesLeft = (1.0 - point.within) * size;
  const double tolerance = total * 1e-6 + 1.0;
  if (bytesLeft > tolerance) return point;

  for (int next = point.spineIndex + 1; next < spineCount; next++) {
    if (static_cast<double>(cumAt(ctx, next)) > itemStart(next, cumAt, ctx)) {
      SpinePoint snapped;
      snapped.spineIndex = next;
      snapped.within = 0.0;
      return snapped;
    }
  }
  return point;
}

void encodeLanding(const SpinePoint& point, const int spineCount, uint8_t (&out)[LANDING_RECORD_SIZE]) {
  const double within = std::clamp(point.within, 0.0, 1.0);
  std::copy(std::begin(LANDING_MAGIC), std::end(LANDING_MAGIC), out);
  putLe(out + 4, static_cast<uint32_t>(std::clamp(point.spineIndex, 0, 0xFFFF)), 2);
  putLe(out + 6, static_cast<uint32_t>(std::clamp(spineCount, 0, 0xFFFF)), 2);
  putLe(out + 8, static_cast<uint32_t>(within * WITHIN_SCALE + 0.5), 4);
}

std::optional<SpinePoint> decodeLanding(const uint8_t* data, const size_t length, const int spineCount) {
  if (data == nullptr || length != LANDING_RECORD_SIZE) return std::nullopt;
  if (!std::equal(std::begin(LANDING_MAGIC), std::end(LANDING_MAGIC), data)) return std::nullopt;
  const int spine = static_cast<int>(getLe(data + 4, 2));
  const int recordedCount = static_cast<int>(getLe(data + 6, 2));
  const uint32_t withinMillionths = getLe(data + 8, 4);
  if (recordedCount != spineCount || spine >= spineCount || withinMillionths > WITHIN_SCALE) return std::nullopt;
  SpinePoint point;
  point.spineIndex = spine;
  point.within = static_cast<double>(withinMillionths) / WITHIN_SCALE;
  return point;
}

}  // namespace BookSync
