#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>

#include "BookSyncConfig.h"
#include "BookSyncLanding.h"
#include "BookSyncPatience.h"
#include "vectors/vectors_kosync.h"

// vectors/vectors_kosync.h is copied verbatim from the wiphone repo (tools/gen_kosync_vectors.py
// writes it): the WiPhone and COVEY suites check the same numbers, so every device lands a
// percentage in the same chapter.

namespace {
size_t cumFromArray(const void* ctx, const int index) { return static_cast<const uint32_t*>(ctx)[index]; }

std::optional<BookSync::SpinePoint> locate(const KsBook& book, const double p) {
  return BookSync::locateByteWeighted(p, book.nCp, cumFromArray, book.cum);
}

// What the X4 lands on: the pinned inverse, then the chapter-start snap.
BookSync::SpinePoint land(const KsBook& book, const double p) {
  const auto point = locate(book, p);
  EXPECT_TRUE(point.has_value());
  return BookSync::snapToChapterStart(point.value_or(BookSync::SpinePoint{}), book.nCp, cumFromArray, book.cum);
}

double itemSize(const KsBook& book, const int index) {
  return static_cast<double>(book.cum[index]) - (index > 0 ? static_cast<double>(book.cum[index - 1]) : 0.0);
}

// The percentage as it crosses the wire: a JSON number with 6 decimals.
double onTheWire(const double p) {
  char text[32];
  snprintf(text, sizeof(text), "%.6f", p);
  return strtod(text, nullptr);
}
}  // namespace

// --- Server URL by network ------------------------------------------------------------------

TEST(BookSyncServerUrl, PeerNetworkUsesPeerUrl) {
  const BookSync::Config config;
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books", config, "http://192.168.1.20:8088"), "http://192.168.4.1");
}

TEST(BookSyncServerUrl, AnyOtherNetworkUsesConfiguredServer) {
  const BookSync::Config config;
  EXPECT_EQ(BookSync::chooseServerUrl("HomeNet", config, "http://192.168.1.20:8088"), "http://192.168.1.20:8088");
  // SSIDs compare exactly, as 802.11 does.
  EXPECT_EQ(BookSync::chooseServerUrl("wiphone-books", config, "covey.local"), "covey.local");
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books ", config, "covey.local"), "covey.local");
}

TEST(BookSyncServerUrl, NotConnectedUsesConfiguredServer) {
  const BookSync::Config config;
  EXPECT_EQ(BookSync::chooseServerUrl("", config, "covey.local"), "covey.local");
}

TEST(BookSyncServerUrl, EmptyConfiguredServerStaysEmptyForTheStockDefault) {
  // getBaseUrl() turns "" into the stock default server; the peer must not change that elsewhere.
  const BookSync::Config config;
  EXPECT_EQ(BookSync::chooseServerUrl("HomeNet", config, ""), "");
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books", config, ""), "http://192.168.4.1");
}

TEST(BookSyncServerUrl, UnsetPeerSettingsKeepStockBehaviour) {
  BookSync::Config noSsid;
  noSsid.peerSsid.clear();
  EXPECT_EQ(BookSync::chooseServerUrl("", noSsid, "covey.local"), "covey.local");
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books", noSsid, "covey.local"), "covey.local");

  BookSync::Config noUrl;
  noUrl.peerUrl.clear();
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books", noUrl, "covey.local"), "covey.local");
}

TEST(BookSyncServerUrl, CustomPeer) {
  BookSync::Config config;
  config.peerSsid = "Trail Phone";
  config.peerUrl = "10.0.0.1:8080";
  EXPECT_EQ(BookSync::chooseServerUrl("Trail Phone", config, "covey.local"), "10.0.0.1:8080");
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books", config, "covey.local"), "covey.local");
}

// --- Settings defaults ------------------------------------------------------------------------

TEST(BookSyncConfig, Defaults) {
  const BookSync::Config config;
  EXPECT_EQ(config.peerSsid, "WiPhone-Books");
  EXPECT_EQ(config.peerUrl, "http://192.168.4.1");
  EXPECT_EQ(config.peerPassword, "");  // the WiPhone's hotspot is open unless its owner sets a password
  EXPECT_EQ(BookSync::windowMs(config.windowIndex), 300000u);
  EXPECT_FALSE(config.pushOnClose);
  EXPECT_FALSE(config.pullOnOpen);
}

TEST(BookSyncConfig, WindowChoices) {
  EXPECT_EQ(BookSync::WINDOW_COUNT, 5);
  EXPECT_EQ(BookSync::windowMs(0), 0u);
  EXPECT_EQ(BookSync::windowMs(1), 60000u);
  EXPECT_EQ(BookSync::windowMs(2), 120000u);
  EXPECT_EQ(BookSync::windowMs(3), 300000u);
  EXPECT_EQ(BookSync::windowMs(4), 600000u);
}

TEST(BookSyncConfig, CorruptWindowIndexFallsBackToDefault) {
  EXPECT_EQ(BookSync::sanitizeWindowIndex(4), 4);
  EXPECT_EQ(BookSync::sanitizeWindowIndex(5), BookSync::DEFAULT_WINDOW_INDEX);
  EXPECT_EQ(BookSync::sanitizeWindowIndex(255), BookSync::DEFAULT_WINDOW_INDEX);
  EXPECT_EQ(BookSync::windowMs(200), 300000u);
}

TEST(BookSyncConfig, ManualSyncUsesTheWindowAndOffIsTheStockList) {
  EXPECT_EQ(BookSync::patientWindowMs(BookSyncTrigger::Manual, 3), 300000u);
  EXPECT_EQ(BookSync::patientWindowMs(BookSyncTrigger::Manual, 0), 0u);
}

TEST(BookSyncConfig, OpenSyncIsCappedAndNeverOpensTheList) {
  for (uint8_t i = 0; i < BookSync::WINDOW_COUNT; i++) {
    EXPECT_EQ(BookSync::patientWindowMs(BookSyncTrigger::Open, i), BookSync::AUTO_TRIGGER_WINDOW_MS);
  }
}

TEST(BookSyncConfig, CloseSyncUsesTheWindowAndNeverOpensTheList) {
  EXPECT_EQ(BookSync::patientWindowMs(BookSyncTrigger::CloseToHome, 3), 300000u);
  EXPECT_EQ(BookSync::patientWindowMs(BookSyncTrigger::CloseToLibrary, 1), 60000u);
  EXPECT_EQ(BookSync::patientWindowMs(BookSyncTrigger::CloseToHome, 0), BookSync::AUTO_TRIGGER_WINDOW_MS);
  EXPECT_EQ(BookSync::patientWindowMs(BookSyncTrigger::CloseToLibrary, 0), BookSync::AUTO_TRIGGER_WINDOW_MS);
}

TEST(BookSyncConfig, CloseTriggers) {
  EXPECT_TRUE(BookSync::isCloseTrigger(BookSyncTrigger::CloseToHome));
  EXPECT_TRUE(BookSync::isCloseTrigger(BookSyncTrigger::CloseToLibrary));
  EXPECT_FALSE(BookSync::isCloseTrigger(BookSyncTrigger::Manual));
  EXPECT_FALSE(BookSync::isCloseTrigger(BookSyncTrigger::Open));
}

// --- The peer network in the Wi-Fi list ------------------------------------------------------

TEST(BookSyncPeerNetwork, ASyncAddsAMissingPeerAsOpenByDefault) {
  const BookSync::Config config;
  const auto write = BookSync::peerCredentialToWrite(config, std::nullopt, false);
  ASSERT_TRUE(write.has_value());
  EXPECT_EQ(*write, "");
}

TEST(BookSyncPeerNetwork, ASyncAddsAMissingPeerWithItsPassword) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  const auto write = BookSync::peerCredentialToWrite(config, std::nullopt, false);
  ASSERT_TRUE(write.has_value());
  EXPECT_EQ(*write, "trailhead-42");
}

TEST(BookSyncPeerNetwork, ASyncNeverOverwritesAnEntryInTheList) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string(""), false).has_value());
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string("typed-by-hand"), false).has_value());
  config.peerPassword.clear();
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string("typed-by-hand"), false).has_value());
}

TEST(BookSyncPeerNetwork, SavingThePeerSettingsOverwritesTheEntry) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  const auto protect = BookSync::peerCredentialToWrite(config, std::string(""), true);
  ASSERT_TRUE(protect.has_value());
  EXPECT_EQ(*protect, "trailhead-42");

  config.peerPassword.clear();  // back to an open hotspot
  const auto open = BookSync::peerCredentialToWrite(config, std::string("trailhead-42"), true);
  ASSERT_TRUE(open.has_value());
  EXPECT_EQ(*open, "");

  const auto added = BookSync::peerCredentialToWrite(config, std::nullopt, true);
  ASSERT_TRUE(added.has_value());
  EXPECT_EQ(*added, "");
}

TEST(BookSyncPeerNetwork, SavingAnUnchangedPasswordWritesNothing) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string("trailhead-42"), true).has_value());
  config.peerPassword.clear();
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string(""), true).has_value());
}

TEST(BookSyncPeerNetwork, NoPeerSsidWritesNothing) {
  BookSync::Config config;
  config.peerSsid.clear();
  config.peerPassword = "trailhead-42";
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::nullopt, false).has_value());
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::nullopt, true).has_value());
}

TEST(BookSyncPeerNetwork, ProtectedHotspotSavedOpenIsAMismatch) {
  EXPECT_TRUE(BookSync::peerPasswordMismatch(true, true, std::string("")));
}

TEST(BookSyncPeerNetwork, OpenHotspotSavedWithAPasswordIsAMismatch) {
  // A saved password sets a WPA2 minimum, so the station never joins an open network.
  EXPECT_TRUE(BookSync::peerPasswordMismatch(true, false, std::string("trailhead-42")));
}

TEST(BookSyncPeerNetwork, MatchingEntriesAreNotAMismatch) {
  EXPECT_FALSE(BookSync::peerPasswordMismatch(true, false, std::string("")));
  // A protected hotspot with a password saved: whether it is the right one only the join can tell.
  EXPECT_FALSE(BookSync::peerPasswordMismatch(true, true, std::string("trailhead-42")));
}

TEST(BookSyncPeerNetwork, OutOfRangeOrUnsavedIsNotAMismatch) {
  EXPECT_FALSE(BookSync::peerPasswordMismatch(false, false, std::string("")));
  EXPECT_FALSE(BookSync::peerPasswordMismatch(false, true, std::string("")));
  EXPECT_FALSE(BookSync::peerPasswordMismatch(true, true, std::nullopt));
}

// --- Patient connect --------------------------------------------------------------------------

TEST(BookSyncPatience, DisarmedIsTheStockList) {
  BookSyncPatience patience;
  EXPECT_FALSE(patience.armed());
  EXPECT_FALSE(patience.nothingToJoin(1000));
  EXPECT_FALSE(patience.waiting());
  EXPECT_EQ(patience.secondsLeft(1000), 0u);

  patience.arm(0, 1000);
  EXPECT_FALSE(patience.armed());
  EXPECT_FALSE(patience.nothingToJoin(2000));
}

TEST(BookSyncPatience, RescansEveryIntervalUntilTheWindowEnds) {
  BookSyncPatience patience;
  patience.arm(300000, 10000);
  EXPECT_TRUE(patience.armed());
  EXPECT_FALSE(patience.waiting());
  EXPECT_FALSE(patience.takeRescan(10000));  // nothing scheduled before a scan came back empty

  EXPECT_TRUE(patience.nothingToJoin(12000));
  EXPECT_TRUE(patience.waiting());
  EXPECT_FALSE(patience.takeRescan(12000 + BookSyncPatience::RESCAN_INTERVAL_MS - 1));
  EXPECT_TRUE(patience.waiting());
  EXPECT_TRUE(patience.takeRescan(12000 + BookSyncPatience::RESCAN_INTERVAL_MS));
  EXPECT_FALSE(patience.waiting());
  EXPECT_FALSE(patience.takeRescan(12000 + BookSyncPatience::RESCAN_INTERVAL_MS + 1));  // once

  // The next empty scan waits again; the window still counts from arm().
  EXPECT_TRUE(patience.nothingToJoin(20000));
  EXPECT_TRUE(patience.waiting());
  EXPECT_FALSE(patience.expired(309999));
  EXPECT_TRUE(patience.expired(310000));
}

TEST(BookSyncPatience, AnEmptyScanAfterTheWindowGivesUp) {
  BookSyncPatience patience;
  patience.arm(60000, 0);
  EXPECT_TRUE(patience.nothingToJoin(59999));
  EXPECT_TRUE(patience.takeRescan(64999 + 1));
  EXPECT_FALSE(patience.nothingToJoin(65000));
  EXPECT_FALSE(patience.waiting());
}

TEST(BookSyncPatience, ArmingClearsThePasswordHint) {
  BookSyncPatience patience;
  patience.arm(60000, 0);
  EXPECT_FALSE(patience.showPeerPasswordHint());
  patience.setPeerPasswordHint(true);
  EXPECT_TRUE(patience.showPeerPasswordHint());
  patience.arm(60000, 1000);
  EXPECT_FALSE(patience.showPeerPasswordHint());
}

TEST(BookSyncPatience, DisarmStopsWaiting) {
  BookSyncPatience patience;
  patience.arm(60000, 0);
  EXPECT_TRUE(patience.nothingToJoin(1000));
  patience.disarm();
  EXPECT_FALSE(patience.waiting());
  EXPECT_FALSE(patience.armed());
  EXPECT_FALSE(patience.takeRescan(100000));
  EXPECT_FALSE(patience.nothingToJoin(2000));
}

TEST(BookSyncPatience, CountdownRoundsUp) {
  BookSyncPatience patience;
  patience.arm(300000, 0);
  EXPECT_EQ(patience.secondsLeft(0), 300u);
  EXPECT_EQ(patience.secondsLeft(1), 300u);
  EXPECT_EQ(patience.secondsLeft(1000), 299u);
  EXPECT_EQ(patience.secondsLeft(299001), 1u);
  EXPECT_EQ(patience.secondsLeft(300000), 0u);
  EXPECT_EQ(patience.secondsLeft(400000), 0u);
}

TEST(BookSyncPatience, SurvivesTheMillisWrap) {
  BookSyncPatience patience;
  const uint32_t start = 0xFFFFF000u;  // 4096 ms before millis() wraps
  patience.arm(300000, start);
  EXPECT_TRUE(patience.nothingToJoin(start + 1000));
  EXPECT_FALSE(patience.takeRescan(start + 1000 + BookSyncPatience::RESCAN_INTERVAL_MS - 1));
  EXPECT_TRUE(patience.takeRescan(start + 1000 + BookSyncPatience::RESCAN_INTERVAL_MS));  // wrapped
  EXPECT_EQ(patience.secondsLeft(start + 10000), 290u);
  EXPECT_FALSE(patience.expired(start + 299999));
  EXPECT_TRUE(patience.expired(start + 300000));
}

// --- Landing: the byte-weighted inverse -------------------------------------------------------

TEST(BookSyncLanding, InverseReproducesEveryVector) {
  for (const KsBook& book : KS_BOOKS) {
    for (int i = 0; i < book.nInv; i++) {
      const KsInv& v = book.inv[i];
      SCOPED_TRACE(std::string(book.file) + " p=" + std::to_string(v.p));
      const auto point = locate(book, v.p);
      ASSERT_TRUE(point.has_value());
      EXPECT_EQ(point->spineIndex, v.cp);
      EXPECT_DOUBLE_EQ(point->within, v.within);
    }
  }
}

TEST(BookSyncLanding, MidChapterPositionsLandInTheirChapter) {
  for (const KsBook& book : KS_BOOKS) {
    for (int i = 0; i < book.nFwd; i++) {
      const KsFwd& v = book.fwd[i];
      if (v.within != 0.25 && v.within != 0.5) continue;
      SCOPED_TRACE(std::string(book.file) + " r=" + std::to_string(v.r) + " within=" + std::to_string(v.within));
      const auto point = land(book, v.pct);
      EXPECT_EQ(point.spineIndex, book.readingToCp[v.r]);
      EXPECT_NEAR(point.within, v.within, 1e-9);
    }
  }
}

TEST(BookSyncLanding, ChapterStartsLandOnTheChapterNotThePreviousPage) {
  // A chapter start is an exact item boundary, which the pinned inverse resolves to the END of
  // the item before; after 6-decimal rounding it falls either side at random. Both must land
  // on the chapter's own first page.
  for (const KsBook& book : KS_BOOKS) {
    const double total = book.cum[book.nCp - 1];
    for (int i = 0; i < book.nFwd; i++) {
      const KsFwd& v = book.fwd[i];
      if (v.within != 0.0) continue;
      for (const double p : {v.pct, onTheWire(v.pct)}) {
        SCOPED_TRACE(std::string(book.file) + " r=" + std::to_string(v.r) + " p=" + std::to_string(p));
        const auto point = land(book, p);
        EXPECT_EQ(point.spineIndex, book.readingToCp[v.r]);
        EXPECT_LE(point.within * itemSize(book, point.spineIndex), total * 1e-6 + 1.0);
      }
    }
  }
}

TEST(BookSyncLanding, SnapSkipsEmptyItems) {
  // kosync-mixed: "chapter 2.xhtml" (4) ends where the missing file (5, 0 bytes) begins and ends.
  const KsBook& book = KS_BOOKS[0];
  ASSERT_EQ(itemSize(book, 5), 0.0);
  const auto point = land(book, static_cast<double>(book.cum[4]) / book.cum[book.nCp - 1]);
  EXPECT_EQ(point.spineIndex, 6);
  EXPECT_EQ(point.within, 0.0);
}

TEST(BookSyncLanding, EndOfBookStaysOnTheLastItem) {
  for (const KsBook& book : KS_BOOKS) {
    SCOPED_TRACE(book.file);
    const auto point = land(book, 1.0);
    EXPECT_EQ(point.spineIndex, book.nCp - 1);
    EXPECT_EQ(point.within, 1.0);
  }
}

TEST(BookSyncLanding, OutOfRangePercentagesClamp) {
  const KsBook& book = KS_BOOKS[1];
  const auto low = locate(book, -0.5);
  ASSERT_TRUE(low.has_value());
  EXPECT_EQ(low->spineIndex, 0);
  EXPECT_EQ(low->within, 0.0);
  const auto high = locate(book, 7.0);
  ASSERT_TRUE(high.has_value());
  EXPECT_EQ(high->spineIndex, book.nCp - 1);
  EXPECT_EQ(high->within, 1.0);
}

TEST(BookSyncLanding, ABookWithoutBytesIsNotSyncable) {
  const uint32_t empty[] = {0u, 0u, 0u};
  EXPECT_FALSE(BookSync::locateByteWeighted(0.5, 3, cumFromArray, empty).has_value());
  EXPECT_FALSE(BookSync::locateByteWeighted(0.5, 0, cumFromArray, empty).has_value());
}

// --- Landing: the record carried across the reboot --------------------------------------------

TEST(BookSyncLandingRecord, RoundTrip) {
  uint8_t record[BookSync::LANDING_RECORD_SIZE];
  BookSync::SpinePoint point;
  point.spineIndex = 6;
  point.within = 0.123456789;
  BookSync::encodeLanding(point, 8, record);
  const auto decoded = BookSync::decodeLanding(record, sizeof(record), 8);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->spineIndex, 6);
  EXPECT_NEAR(decoded->within, 0.123457, 1e-12);
}

TEST(BookSyncLandingRecord, LayoutIsLittleEndianMillionths) {
  uint8_t record[BookSync::LANDING_RECORD_SIZE];
  BookSync::SpinePoint point;
  point.spineIndex = 0x0102;
  point.within = 0.5;
  BookSync::encodeLanding(point, 0x0304, record);
  const uint8_t expected[BookSync::LANDING_RECORD_SIZE] = {'B',  'S',  'L',  '1',  0x02, 0x01,
                                                           0x04, 0x03, 0x20, 0xA1, 0x07, 0x00};
  for (size_t i = 0; i < sizeof(record); i++) EXPECT_EQ(record[i], expected[i]) << "byte " << i;
}

TEST(BookSyncLandingRecord, EndsOfTheChapterSurvive) {
  uint8_t record[BookSync::LANDING_RECORD_SIZE];
  for (const double within : {0.0, 1.0}) {
    BookSync::SpinePoint point;
    point.spineIndex = 2;
    point.within = within;
    BookSync::encodeLanding(point, 3, record);
    const auto decoded = BookSync::decodeLanding(record, sizeof(record), 3);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->within, within);
  }
}

TEST(BookSyncLandingRecord, EncodeClampsWithin) {
  uint8_t record[BookSync::LANDING_RECORD_SIZE];
  BookSync::SpinePoint point;
  point.spineIndex = 0;
  point.within = 1.5;
  BookSync::encodeLanding(point, 1, record);
  EXPECT_EQ(BookSync::decodeLanding(record, sizeof(record), 1)->within, 1.0);
  point.within = -0.25;
  BookSync::encodeLanding(point, 1, record);
  EXPECT_EQ(BookSync::decodeLanding(record, sizeof(record), 1)->within, 0.0);
}

TEST(BookSyncLandingRecord, RejectsRecordsThatDoNotFitThisBook) {
  uint8_t record[BookSync::LANDING_RECORD_SIZE];
  BookSync::SpinePoint point;
  point.spineIndex = 4;
  point.within = 0.25;
  BookSync::encodeLanding(point, 8, record);

  EXPECT_FALSE(BookSync::decodeLanding(record, sizeof(record), 9).has_value());  // the book changed
  EXPECT_FALSE(BookSync::decodeLanding(record, sizeof(record), 4).has_value());  // spine out of range
  EXPECT_FALSE(BookSync::decodeLanding(record, sizeof(record) - 1, 8).has_value());
  EXPECT_FALSE(BookSync::decodeLanding(record, sizeof(record) + 1, 8).has_value());
  EXPECT_FALSE(BookSync::decodeLanding(nullptr, sizeof(record), 8).has_value());

  uint8_t badMagic[BookSync::LANDING_RECORD_SIZE];
  std::copy(std::begin(record), std::end(record), badMagic);
  badMagic[3] = '2';
  EXPECT_FALSE(BookSync::decodeLanding(badMagic, sizeof(badMagic), 8).has_value());

  uint8_t badWithin[BookSync::LANDING_RECORD_SIZE];
  std::copy(std::begin(record), std::end(record), badWithin);
  badWithin[8] = 0x41;  // 1,000,001 millionths
  badWithin[9] = 0x42;
  badWithin[10] = 0x0F;
  badWithin[11] = 0x00;
  EXPECT_FALSE(BookSync::decodeLanding(badWithin, sizeof(badWithin), 8).has_value());
}
