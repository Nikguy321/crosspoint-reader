#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// When this device's position may go over the server's record, and what the
// book last agreed with a server. Pure decisions and the record format; the
// file I/O lives in src/util/BookSyncHooks.
namespace BookSync {

// Percentages at most this far apart are the same position (CrossPoint's
// "synchronized"; the WiPhone and COVEY use the same rule).
constexpr float SAME_POSITION_EPSILON = 0.001f;

// What KOReaderSyncClient sends as device_id and device. Every CrossPoint sends
// the same pair, so a record from another CrossPoint reads as this device's own.
constexpr char OWN_DEVICE_ID[] = "crosspoint-reader";
constexpr char OWN_DEVICE_NAME[] = "CrossPoint";

// More than the epsilon apart. A non-finite value always counts as moved.
bool positionMoved(float from, float to);

// The percentage as it may go on the wire: [0, 1], a non-finite value as 0.
float wirePercentage(float percentage);

// Every upload is stored under both document ids (KOReader's partial MD5 and
// the MD5 of the file name), so a reader matching the book either way finds it:
// the id for the second PUT, or nullopt when there is no distinct other id.
std::optional<std::string> secondDocumentId(const std::string& primaryId, const std::string& alternateId);

// The book's last agreement with a server: after an upload or "already
// synchronized" (At), or after a remote record was applied (Rebase: the landing
// page, and so this book's own percentage, is only known once it reopens).
struct SyncedRecord {
  enum class Kind : uint8_t { None, At, Rebase };
  Kind kind = Kind::None;
  float local = 0.0f;   // At: this book's percentage
  float remote = 0.0f;  // At and Rebase: the server's percentage
};

// Kept in the book's cache dir: magic "BSS1", this book's percentage, the
// server's percentage, each in millionths (u32, little-endian); a local value of
// 0xFFFFFFFF is Rebase.
constexpr size_t SYNCED_RECORD_SIZE = 12;
void encodeSynced(const SyncedRecord& record, uint8_t (&out)[SYNCED_RECORD_SIZE]);
// A wrong length, magic or value reads as None (never synced).
SyncedRecord decodeSynced(const uint8_t* data, size_t length);

// Rebase -> At once the book has reopened on the applied record's landing page.
SyncedRecord rebased(const SyncedRecord& record, float openPercentage);

// The automatic sync on leaving a book pushes only a position that moved since
// the book was opened or since its last sync, so an unmoved book never goes over
// a newer record. Never from the end-of-book screen (its spine index is one past
// the last item). A book never synced counts as moved: its place has never
// reached a server, and another device's record there is offered rather than
// uploaded over (smartOverride). `openPercentage` is empty when the first page
// never reached the panel.
bool closeSyncWanted(bool atEndOfBook, std::optional<float> openPercentage, float nowPercentage,
                     const SyncedRecord& synced);

// D1's "another device": the record's device_id, or its device name when it has
// no device_id. A 200 {} (no record) names no device, and ArduinoJson reads its
// missing strings as "null".
bool fromAnotherDevice(const std::string& deviceId, const std::string& device);

// What the smart sync does instead of its own choice when the positions differ
// (D1). The server's record is the one this book last agreed with when it
// matches the synced record's server percentage, whichever server that was:
//  - AlreadySynced: that record, and this book has not moved since (it landed on
//    a page near it); uploading the page's own percentage would only echo it
//    back to the device that sent it, and applying it would reopen the same page.
//  - Upload: that record, and this book moved since, either way. The record is
//    older than the move, so the move is pushed; applying it (the stock choice
//    when the book went back) would silently undo the move.
//  - Offer: another device's place, in any other case. Shown on the compare
//    screen, with the stock choice preselected, instead of being uploaded over or
//    applied unseen: this device keeps no time of its last move, so nothing else
//    proves that record older than it (D1: unknown -> offer). Neither a book
//    never synced nor one that moved since its last sync changes that.
//  - None: this device's own record (every CrossPoint sends the same device id)
//    or no record at all (200 {} reads as 0 %): the stock smart choice.
enum class SmartOverride : uint8_t { None, AlreadySynced, Upload, Offer };
SmartOverride smartOverride(const SyncedRecord& synced, float localPercentage, float remotePercentage,
                            bool remoteFromAnotherDevice);

}  // namespace BookSync
