#include "BookSyncPush.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace BookSync {

namespace {
constexpr uint8_t SYNCED_MAGIC[4] = {'B', 'S', 'S', '1'};
constexpr uint32_t PERCENT_SCALE = 1000000;
constexpr uint32_t REBASE_VALUE = 0xFFFFFFFFU;

bool namesDevice(const std::string& value) { return !value.empty() && value != "null"; }

uint32_t toMillionths(const float percentage) {
  return static_cast<uint32_t>(static_cast<double>(wirePercentage(percentage)) * PERCENT_SCALE + 0.5);
}

float fromMillionths(const uint32_t value) { return static_cast<float>(static_cast<double>(value) / PERCENT_SCALE); }

void putLe32(uint8_t* out, const uint32_t value) {
  for (int i = 0; i < 4; i++) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t getLe32(const uint8_t* in) {
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) value |= static_cast<uint32_t>(in[i]) << (8 * i);
  return value;
}
}  // namespace

bool positionMoved(const float from, const float to) {
  if (!std::isfinite(from) || !std::isfinite(to)) return true;
  return std::fabs(from - to) > SAME_POSITION_EPSILON;
}

float wirePercentage(const float percentage) {
  if (!std::isfinite(percentage)) return 0.0f;
  return std::clamp(percentage, 0.0f, 1.0f);
}

std::optional<std::string> secondDocumentId(const std::string& primaryId, const std::string& alternateId) {
  if (alternateId.empty() || alternateId == primaryId) return std::nullopt;
  return alternateId;
}

void encodeSynced(const SyncedRecord& record, uint8_t (&out)[SYNCED_RECORD_SIZE]) {
  std::copy(std::begin(SYNCED_MAGIC), std::end(SYNCED_MAGIC), out);
  putLe32(out + 4, record.kind == SyncedRecord::Kind::At ? toMillionths(record.local) : REBASE_VALUE);
  putLe32(out + 8, toMillionths(record.remote));
}

SyncedRecord decodeSynced(const uint8_t* data, const size_t length) {
  SyncedRecord record;
  if (data == nullptr || length != SYNCED_RECORD_SIZE) return record;
  if (!std::equal(std::begin(SYNCED_MAGIC), std::end(SYNCED_MAGIC), data)) return record;
  const uint32_t local = getLe32(data + 4);
  const uint32_t remote = getLe32(data + 8);
  if (remote > PERCENT_SCALE || (local > PERCENT_SCALE && local != REBASE_VALUE)) return record;
  record.kind = local == REBASE_VALUE ? SyncedRecord::Kind::Rebase : SyncedRecord::Kind::At;
  record.local = local == REBASE_VALUE ? 0.0f : fromMillionths(local);
  record.remote = fromMillionths(remote);
  return record;
}

SyncedRecord rebased(const SyncedRecord& record, const float openPercentage) {
  if (record.kind != SyncedRecord::Kind::Rebase) return record;
  SyncedRecord landed = record;
  landed.kind = SyncedRecord::Kind::At;
  landed.local = wirePercentage(openPercentage);
  return landed;
}

bool closeSyncWanted(const bool atEndOfBook, const std::optional<float> openPercentage, const float nowPercentage,
                     const SyncedRecord& synced) {
  if (atEndOfBook) return false;
  if (openPercentage && positionMoved(*openPercentage, nowPercentage)) return true;
  switch (synced.kind) {
    case SyncedRecord::Kind::None:
      return true;
    case SyncedRecord::Kind::At:
      return positionMoved(synced.local, nowPercentage);
    case SyncedRecord::Kind::Rebase:
    default:
      // Still on the applied record's landing: the first page never reached the panel.
      return false;
  }
}

bool fromAnotherDevice(const std::string& deviceId, const std::string& device) {
  if (namesDevice(deviceId)) return deviceId != OWN_DEVICE_ID;
  return namesDevice(device) && device != OWN_DEVICE_NAME;
}

SmartOverride smartOverride(const SyncedRecord& synced, const float localPercentage, const float remotePercentage,
                            const bool remoteFromAnotherDevice) {
  if (synced.kind == SyncedRecord::Kind::At && !positionMoved(synced.remote, remotePercentage)) {
    return positionMoved(synced.local, localPercentage) ? SmartOverride::Upload : SmartOverride::AlreadySynced;
  }
  if (remoteFromAnotherDevice && positionMoved(localPercentage, remotePercentage)) return SmartOverride::Offer;
  return SmartOverride::None;
}

}  // namespace BookSync
