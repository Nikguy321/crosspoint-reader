#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>

#include "BookSyncConfig.h"
#include "BookSyncLanding.h"
#include "BookSyncPatience.h"
#include "BookSyncPush.h"
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

namespace {
// COVEY's game hotspot: its own address, not the home LAN one the configured server names.
BookSync::Config withCoveyHub() {
  BookSync::Config config;
  config.hubSsid = "COVEY";
  config.hubUrl = "http://192.168.89.1:8088";
  return config;
}
}  // namespace

TEST(BookSyncServerUrl, HubNetworkUsesHubUrl) {
  const BookSync::Config config = withCoveyHub();
  EXPECT_EQ(BookSync::chooseServerUrl("COVEY", config, "http://192.168.1.55:8088"), "http://192.168.89.1:8088");
  EXPECT_EQ(BookSync::chooseServerUrl("SmithWifi", config, "http://192.168.1.55:8088"), "http://192.168.1.55:8088");
  EXPECT_EQ(BookSync::chooseServerUrl("covey", config, "http://192.168.1.55:8088"), "http://192.168.1.55:8088");
  // The peer pair is untouched by the hub pair.
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books", config, "http://192.168.1.55:8088"), "http://192.168.4.1");
}

TEST(BookSyncServerUrl, HubIsOffByDefaultAndWithAnEmptyHalf) {
  const BookSync::Config defaults;
  EXPECT_EQ(defaults.hubSsid, "");
  EXPECT_EQ(defaults.hubUrl, "");
  EXPECT_EQ(BookSync::chooseServerUrl("", defaults, "covey.local"), "covey.local");
  EXPECT_EQ(BookSync::chooseServerUrl("COVEY", defaults, "covey.local"), "covey.local");

  BookSync::Config noUrl = withCoveyHub();
  noUrl.hubUrl.clear();
  EXPECT_EQ(BookSync::chooseServerUrl("COVEY", noUrl, "covey.local"), "covey.local");
  BookSync::Config noSsid = withCoveyHub();
  noSsid.hubSsid.clear();
  EXPECT_EQ(BookSync::chooseServerUrl("", noSsid, "covey.local"), "covey.local");
}

TEST(BookSyncServerUrl, PeerWinsWhenBothNameTheSameNetwork) {
  BookSync::Config config = withCoveyHub();
  config.hubSsid = "WiPhone-Books";
  EXPECT_EQ(BookSync::chooseServerUrl("WiPhone-Books", config, "covey.local"), "http://192.168.4.1");
}

TEST(BookSyncServerUrl, NoPlaintextPasswordOnADeviceNetwork) {
  const BookSync::Config config = withCoveyHub();
  EXPECT_TRUE(BookSync::onDeviceNetwork("WiPhone-Books", config));
  EXPECT_TRUE(BookSync::onDeviceNetwork("COVEY", config));
  EXPECT_FALSE(BookSync::onDeviceNetwork("SmithWifi", config));
  EXPECT_FALSE(BookSync::onDeviceNetwork("", config));

  // The peer's hotspot is open by default: never the Basic header there, whichever server is used.
  BookSync::Config peerNoUrl;
  peerNoUrl.peerUrl.clear();
  EXPECT_TRUE(BookSync::onDeviceNetwork("WiPhone-Books", peerNoUrl));
  // A hub with no URL is just another network, whose configured server may want Basic auth.
  BookSync::Config hubNoUrl = withCoveyHub();
  hubNoUrl.hubUrl.clear();
  EXPECT_FALSE(BookSync::onDeviceNetwork("COVEY", hubNoUrl));
  BookSync::Config noPeer;
  noPeer.peerSsid.clear();
  EXPECT_FALSE(BookSync::onDeviceNetwork("", noPeer));
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
  const auto write = BookSync::peerCredentialToWrite(config, std::nullopt, BookSync::PeerWrite::SyncStart);
  ASSERT_TRUE(write.has_value());
  EXPECT_EQ(*write, "");
}

TEST(BookSyncPeerNetwork, ASyncAddsAMissingPeerWithItsPassword) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  const auto write = BookSync::peerCredentialToWrite(config, std::nullopt, BookSync::PeerWrite::SyncStart);
  ASSERT_TRUE(write.has_value());
  EXPECT_EQ(*write, "trailhead-42");
}

TEST(BookSyncPeerNetwork, ASyncNeverOverwritesAnEntryInTheList) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string(""), BookSync::PeerWrite::SyncStart).has_value());
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string("typed-by-hand"), BookSync::PeerWrite::SyncStart)
                   .has_value());
  config.peerPassword.clear();
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string("typed-by-hand"), BookSync::PeerWrite::SyncStart)
                   .has_value());
}

TEST(BookSyncPeerNetwork, SavingThePeerPasswordOverwritesTheEntry) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  const auto protect = BookSync::peerCredentialToWrite(config, std::string(""), BookSync::PeerWrite::PasswordSaved);
  ASSERT_TRUE(protect.has_value());
  EXPECT_EQ(*protect, "trailhead-42");

  config.peerPassword.clear();  // back to an open hotspot
  const auto open =
      BookSync::peerCredentialToWrite(config, std::string("trailhead-42"), BookSync::PeerWrite::PasswordSaved);
  ASSERT_TRUE(open.has_value());
  EXPECT_EQ(*open, "");

  const auto added = BookSync::peerCredentialToWrite(config, std::nullopt, BookSync::PeerWrite::PasswordSaved);
  ASSERT_TRUE(added.has_value());
  EXPECT_EQ(*added, "");
}

TEST(BookSyncPeerNetwork, ANewPeerNameKeepsAPasswordSavedInTheWifiList) {
  // Peer Wi-Fi Name set to a network the Wi-Fi list already holds with its password, before
  // the Peer Wi-Fi Password was typed: the saved password must survive (the name row used to
  // write like the password row and saved the network open).
  BookSync::Config config;
  config.peerSsid = "Pixel-7";
  EXPECT_FALSE(
      BookSync::peerCredentialToWrite(config, std::string("hunter2222"), BookSync::PeerWrite::NameSaved).has_value());
  // Nor does a name saved after the password replace a different one saved from the Wi-Fi list.
  config.peerPassword = "hunter3333";
  EXPECT_FALSE(
      BookSync::peerCredentialToWrite(config, std::string("hunter2222"), BookSync::PeerWrite::NameSaved).has_value());
  config.peerPassword.clear();
  // A missing entry is still added.
  const auto added = BookSync::peerCredentialToWrite(config, std::nullopt, BookSync::PeerWrite::NameSaved);
  ASSERT_TRUE(added.has_value());
  EXPECT_EQ(*added, "");
  // Typing the password is what replaces it.
  config.peerPassword = "hunter3333";
  const auto typed =
      BookSync::peerCredentialToWrite(config, std::string("hunter2222"), BookSync::PeerWrite::PasswordSaved);
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(*typed, "hunter3333");
}

TEST(BookSyncPeerNetwork, AnUnreadableWifiListIsNeverWrittenBack) {
  EXPECT_FALSE(BookSync::wifiListWritable(false, true));  // the file is there but did not load
  EXPECT_TRUE(BookSync::wifiListWritable(false, false));  // first use: no file yet
  EXPECT_TRUE(BookSync::wifiListWritable(true, true));
}

TEST(BookSyncPeerNetwork, APeerTheListCannotTakeIsJoinedDirectly) {
  const BookSync::Config config;
  EXPECT_TRUE(BookSync::joinPeerDirectly(config, true, false, false));
  EXPECT_FALSE(BookSync::joinPeerDirectly(config, true, false, true));    // once per scan
  EXPECT_FALSE(BookSync::joinPeerDirectly(config, true, true, false));    // saved: the stock auto-connect joins it
  EXPECT_FALSE(BookSync::joinPeerDirectly(config, false, false, false));  // not in range
  BookSync::Config noPeer;
  noPeer.peerSsid.clear();
  EXPECT_FALSE(BookSync::joinPeerDirectly(noPeer, true, false, false));
}

TEST(BookSyncPeerNetwork, SavingAnUnchangedPasswordWritesNothing) {
  BookSync::Config config;
  config.peerPassword = "trailhead-42";
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::string("trailhead-42"), BookSync::PeerWrite::PasswordSaved)
                   .has_value());
  config.peerPassword.clear();
  EXPECT_FALSE(
      BookSync::peerCredentialToWrite(config, std::string(""), BookSync::PeerWrite::PasswordSaved).has_value());
}

TEST(BookSyncPeerNetwork, NoPeerSsidWritesNothing) {
  BookSync::Config config;
  config.peerSsid.clear();
  config.peerPassword = "trailhead-42";
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::nullopt, BookSync::PeerWrite::SyncStart).has_value());
  EXPECT_FALSE(BookSync::peerCredentialToWrite(config, std::nullopt, BookSync::PeerWrite::PasswordSaved).has_value());
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

// --- When this device's position goes over the server's (D2) ---------------------------------

namespace {
BookSync::SyncedRecord syncedAt(const float local, const float remote) {
  BookSync::SyncedRecord record;
  record.kind = BookSync::SyncedRecord::Kind::At;
  record.local = local;
  record.remote = remote;
  return record;
}

BookSync::SyncedRecord applied(const float remote) {
  BookSync::SyncedRecord record;
  record.kind = BookSync::SyncedRecord::Kind::Rebase;
  record.remote = remote;
  return record;
}

// Epub::calculateProgress on the end-of-book screen, where the spine index is one past the last
// item: getCumulativeSpineItemSize() answers 0 there and the 32-bit size_t difference wraps.
float endOfBookPercentage(const KsBook& book) {
  const uint32_t total = book.cum[book.nCp - 1];
  const uint32_t previous = total;  // cum[spineCount - 1]
  const uint32_t current = 0u - previous;
  return (static_cast<float>(previous) + 1.0f * static_cast<float>(current)) / static_cast<float>(total);
}
}  // namespace

TEST(BookSyncPush, TheEpsilonIsCrossPointsSynchronizedRule) {
  EXPECT_FALSE(BookSync::positionMoved(0.4f, 0.4f));
  EXPECT_FALSE(BookSync::positionMoved(0.4f, 0.4009f));
  EXPECT_TRUE(BookSync::positionMoved(0.4f, 0.4012f));
  EXPECT_TRUE(BookSync::positionMoved(0.4012f, 0.4f));
  EXPECT_TRUE(BookSync::positionMoved(std::nanf(""), 0.4f));
  EXPECT_TRUE(BookSync::positionMoved(0.4f, INFINITY));
}

TEST(BookSyncPush, TheWireNeverCarriesAPercentagePastTheBook) {
  const KsBook& book = KS_BOOKS[0];
  const float wrapped = endOfBookPercentage(book);
  EXPECT_GT(wrapped, 1000.0f);  // about 2^32 / total: what the end-of-book screen used to upload
  EXPECT_EQ(BookSync::wirePercentage(wrapped), 1.0f);
  EXPECT_EQ(BookSync::wirePercentage(-0.25f), 0.0f);
  EXPECT_EQ(BookSync::wirePercentage(std::nanf("")), 0.0f);
  EXPECT_EQ(BookSync::wirePercentage(0.231f), 0.231f);
  EXPECT_EQ(BookSync::wirePercentage(1.0f), 1.0f);
}

TEST(BookSyncPush, EveryUploadGoesUnderBothIds) {
  for (const KsBook& book : KS_BOOKS) {
    // Binary primary (the fork's default), filename second; and the reverse for a FILENAME config.
    const auto second = BookSync::secondDocumentId(book.partialMd5, book.filenameMd5);
    ASSERT_TRUE(second.has_value()) << book.file;
    EXPECT_EQ(*second, book.filenameMd5);
    const auto reverse = BookSync::secondDocumentId(book.filenameMd5, book.partialMd5);
    ASSERT_TRUE(reverse.has_value()) << book.file;
    EXPECT_EQ(*reverse, book.partialMd5);
  }
  EXPECT_FALSE(BookSync::secondDocumentId(KS_TXT[0].partialMd5, KS_TXT[0].partialMd5).has_value());
  EXPECT_FALSE(BookSync::secondDocumentId(KS_TXT[0].partialMd5, "").has_value());  // the hash failed
}

TEST(BookSyncPush, AnUnmovedCloseNeverGoesOverANewerRecord) {
  // Synced at 40 %, opened at 40 %, closed at 40 %: nothing is sent, whatever the server holds now.
  EXPECT_FALSE(BookSync::closeSyncWanted(false, 0.40f, 0.40f, syncedAt(0.40f, 0.40f)));
  // Within the epsilon is the same position.
  EXPECT_FALSE(BookSync::closeSyncWanted(false, 0.40f, 0.4008f, syncedAt(0.40f, 0.40f)));
}

TEST(BookSyncPush, AMoveSinceOpeningIsPushedOnClose) {
  EXPECT_TRUE(BookSync::closeSyncWanted(false, 0.40f, 0.45f, syncedAt(0.40f, 0.40f)));
  // Back to where the book was last synced, but it moved since opening: pushed, as D2 says.
  EXPECT_TRUE(BookSync::closeSyncWanted(false, 0.30f, 0.40f, syncedAt(0.40f, 0.40f)));
}

TEST(BookSyncPush, AMoveThatNeverReachedAServerIsPushedOnTheNextClose) {
  // Read offline to 55 % last time (that close found no server); opened and closed at 55 % now.
  EXPECT_TRUE(BookSync::closeSyncWanted(false, 0.55f, 0.55f, syncedAt(0.40f, 0.40f)));
  // A book never synced has never shared its place. Another device's record there is offered, never
  // uploaded over (AnotherDevicesPlaceIsOfferedWhateverThisBookDid).
  EXPECT_TRUE(BookSync::closeSyncWanted(false, 0.55f, 0.55f, BookSync::SyncedRecord{}));
  // Closed before the first page reached the panel: only the record decides.
  EXPECT_TRUE(BookSync::closeSyncWanted(false, std::nullopt, 0.55f, syncedAt(0.40f, 0.40f)));
  EXPECT_FALSE(BookSync::closeSyncWanted(false, std::nullopt, 0.40f, syncedAt(0.40f, 0.40f)));
}

TEST(BookSyncPush, TheEndOfBookScreenNeverSyncsOnClose) {
  const float wrapped = endOfBookPercentage(KS_BOOKS[1]);
  EXPECT_FALSE(BookSync::closeSyncWanted(true, 0.98f, wrapped, syncedAt(0.40f, 0.40f)));
  EXPECT_FALSE(BookSync::closeSyncWanted(true, std::nullopt, wrapped, BookSync::SyncedRecord{}));
}

TEST(BookSyncPush, AnAppliedRecordIsSyncedAtThePageItLandedOn) {
  // The remote 45.23 % landed on a page that starts at 45.10 %.
  const BookSync::SyncedRecord landed = BookSync::rebased(applied(0.4523f), 0.4510f);
  EXPECT_EQ(landed.kind, BookSync::SyncedRecord::Kind::At);
  EXPECT_FLOAT_EQ(landed.local, 0.4510f);
  EXPECT_FLOAT_EQ(landed.remote, 0.4523f);
  // Opening and closing on that page sends nothing.
  EXPECT_FALSE(BookSync::closeSyncWanted(false, 0.4510f, 0.4510f, landed));
  // Before it reopened (closed before the first page), it has not moved either.
  EXPECT_FALSE(BookSync::closeSyncWanted(false, std::nullopt, 0.4510f, applied(0.4523f)));
  // Anything but a pending landing is left alone.
  const BookSync::SyncedRecord at = syncedAt(0.2f, 0.2f);
  EXPECT_FLOAT_EQ(BookSync::rebased(at, 0.9f).local, 0.2f);
  EXPECT_EQ(BookSync::rebased(BookSync::SyncedRecord{}, 0.9f).kind, BookSync::SyncedRecord::Kind::None);
}

TEST(BookSyncPush, AnotherDeviceIsNamedByItsDeviceId) {
  EXPECT_TRUE(BookSync::fromAnotherDevice("covey-1", "COVEY"));
  EXPECT_TRUE(BookSync::fromAnotherDevice("wiphone-8c4b14", "WiPhone-NICK"));
  EXPECT_FALSE(BookSync::fromAnotherDevice("crosspoint-reader", "CrossPoint"));
  // The id decides even when the names disagree.
  EXPECT_FALSE(BookSync::fromAnotherDevice("crosspoint-reader", "WiPhone-NICK"));
  EXPECT_TRUE(BookSync::fromAnotherDevice("wiphone-8c4b14", "CrossPoint"));
}

TEST(BookSyncPush, WithoutADeviceIdTheNameDecides) {
  // ArduinoJson reads a missing string as "null".
  for (const auto& parse : KS_PARSE) {
    if (!parse.has) continue;
    const std::string device = parse.device;
    EXPECT_EQ(BookSync::fromAnotherDevice("null", device), device != "CrossPoint") << parse.body;
    EXPECT_EQ(BookSync::fromAnotherDevice("", device), device != "CrossPoint") << parse.body;
  }
  // 200 {}: no record, so no device.
  EXPECT_FALSE(BookSync::fromAnotherDevice("null", "null"));
  EXPECT_FALSE(BookSync::fromAnotherDevice("", ""));
}

TEST(BookSyncPush, AnotherDevicesNewerRecordBehindThisOneIsOffered) {
  // Synced at 55 %; the WiPhone's reader went back to 30 % and pushed; this book has not moved.
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.55f, 0.30f, true), BookSync::SmartOverride::Offer);
}

TEST(BookSyncPush, AMoveBackFromTheSyncedRecordIsPushedNotUndone) {
  // Synced at 55 %, then taken back to 40 % on purpose; the server still holds the 55 % this book agreed
  // with. The stock choice (remote ahead) would apply it with no card and lose the move.
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.40f, 0.55f, true), BookSync::SmartOverride::Upload);
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.40f, 0.55f, false), BookSync::SmartOverride::Upload);
  // Forward from it is pushed too (the stock choice as well), whoever wrote the record.
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.60f, 0.55f, true), BookSync::SmartOverride::Upload);
  // From the page an applied record landed on: the record is still the one the book agreed with.
  const BookSync::SyncedRecord landed = BookSync::rebased(applied(0.4523f), 0.4510f);
  EXPECT_EQ(BookSync::smartOverride(landed, 0.3000f, 0.4523f, true), BookSync::SmartOverride::Upload);
}

TEST(BookSyncPush, TheSyncedRecordOnAnotherServerCountsTheSame) {
  // Synced with COVEY at home at 55 %; in the woods the phone's window answers its own place. The same 55 %
  // is the record this book agreed with, the server does not matter.
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.55f, 0.5503f, true),
            BookSync::SmartOverride::AlreadySynced);
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.40f, 0.5503f, true), BookSync::SmartOverride::Upload);
  // The phone never pulled and is still at 30 %: its place is shown, not uploaded over unseen. The compare
  // screen preselects Upload (local ahead), so a reflexive Confirm still hands 55 % to the phone.
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.55f, 0.30f, true), BookSync::SmartOverride::Offer);
}

TEST(BookSyncPush, AnotherDevicesPlaceIsOfferedWhateverThisBookDid) {
  // Both moved since the last sync: nothing proves the other device's place older than this book's move.
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.40f, 0.40f), 0.55f, 0.30f, true), BookSync::SmartOverride::Offer);
  // A book never synced (every book on the first close after this build, a cleared cache, a new path).
  EXPECT_EQ(BookSync::smartOverride(BookSync::SyncedRecord{}, 0.55f, 0.30f, true), BookSync::SmartOverride::Offer);
  EXPECT_EQ(BookSync::smartOverride(BookSync::SyncedRecord{}, 0.30f, 0.55f, true), BookSync::SmartOverride::Offer);
  // A landing still pending.
  EXPECT_EQ(BookSync::smartOverride(applied(0.30f), 0.55f, 0.30f, true), BookSync::SmartOverride::Offer);
}

TEST(BookSyncPush, AnotherDevicesPlaceAheadIsNeverAppliedUnseen) {
  // SMART would apply it with no card, on a close or an open as well as on Sync progress (D1).
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.30f, 0.30f), 0.30f, 0.55f, true), BookSync::SmartOverride::Offer);
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.30f, 0.30f), 0.35f, 0.55f, true), BookSync::SmartOverride::Offer);
  EXPECT_EQ(BookSync::smartOverride(BookSync::SyncedRecord{}, 0.30f, 0.55f, true), BookSync::SmartOverride::Offer);
}

TEST(BookSyncPush, ThisDevicesOwnRecordOrNoRecordIsLeftToTheSmartSync) {
  // A CrossPoint's own id, or 200 {} read as 0 %.
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.55f, 0.30f, false), BookSync::SmartOverride::None);
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.55f, 0.55f), 0.55f, 0.0f, false), BookSync::SmartOverride::None);
  EXPECT_EQ(BookSync::smartOverride(BookSync::SyncedRecord{}, 0.55f, 0.0f, false), BookSync::SmartOverride::None);
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.30f, 0.30f), 0.30f, 0.55f, false), BookSync::SmartOverride::None);
}

TEST(BookSyncPush, TheSamePlaceIsNeverOffered) {
  // Within the epsilon (the smart sync may still call it ahead by page).
  EXPECT_EQ(BookSync::smartOverride(BookSync::SyncedRecord{}, 0.5500f, 0.5508f, true), BookSync::SmartOverride::None);
  EXPECT_EQ(BookSync::smartOverride(syncedAt(0.40f, 0.40f), 0.5500f, 0.5508f, true), BookSync::SmartOverride::None);
}

TEST(BookSyncPush, TheRecordThisBookLandedOnIsNotEchoedBack) {
  // Applied 45.23 %, landed on a page at 45.10 %, unmoved: the server still holds 45.23 %.
  const BookSync::SyncedRecord landed = BookSync::rebased(applied(0.4523f), 0.4510f);
  EXPECT_EQ(BookSync::smartOverride(landed, 0.4510f, 0.4523f, true), BookSync::SmartOverride::AlreadySynced);
  // A landing just past the record reads LocalAhead; still nothing to upload.
  const BookSync::SyncedRecord past = BookSync::rebased(applied(0.4523f), 0.4538f);
  EXPECT_EQ(BookSync::smartOverride(past, 0.4538f, 0.4523f, true), BookSync::SmartOverride::AlreadySynced);
}

// --- The synced record in the cache dir --------------------------------------------------------

TEST(BookSyncSyncedRecord, RoundTrip) {
  uint8_t record[BookSync::SYNCED_RECORD_SIZE];
  BookSync::encodeSynced(syncedAt(0.123456789f, 0.5f), record);
  const BookSync::SyncedRecord decoded = BookSync::decodeSynced(record, sizeof(record));
  EXPECT_EQ(decoded.kind, BookSync::SyncedRecord::Kind::At);
  EXPECT_NEAR(decoded.local, 0.123457f, 1e-7);
  EXPECT_FLOAT_EQ(decoded.remote, 0.5f);

  BookSync::encodeSynced(applied(1.0f), record);
  const BookSync::SyncedRecord rebase = BookSync::decodeSynced(record, sizeof(record));
  EXPECT_EQ(rebase.kind, BookSync::SyncedRecord::Kind::Rebase);
  EXPECT_FLOAT_EQ(rebase.remote, 1.0f);
}

TEST(BookSyncSyncedRecord, LayoutIsLittleEndianMillionths) {
  uint8_t record[BookSync::SYNCED_RECORD_SIZE];
  BookSync::encodeSynced(syncedAt(0.5f, 0.25f), record);
  const uint8_t expected[BookSync::SYNCED_RECORD_SIZE] = {'B',  'S',  'S',  '1',  0x20, 0xA1,
                                                          0x07, 0x00, 0x90, 0xD0, 0x03, 0x00};
  for (size_t i = 0; i < sizeof(record); i++) EXPECT_EQ(record[i], expected[i]) << "byte " << i;
  BookSync::encodeSynced(applied(0.0f), record);
  for (size_t i = 4; i < 8; i++) EXPECT_EQ(record[i], 0xFF) << "byte " << i;
}

TEST(BookSyncSyncedRecord, EncodeClampsToTheBook) {
  uint8_t record[BookSync::SYNCED_RECORD_SIZE];
  BookSync::encodeSynced(syncedAt(endOfBookPercentage(KS_BOOKS[0]), -1.0f), record);
  const BookSync::SyncedRecord decoded = BookSync::decodeSynced(record, sizeof(record));
  EXPECT_EQ(decoded.kind, BookSync::SyncedRecord::Kind::At);
  EXPECT_EQ(decoded.local, 1.0f);
  EXPECT_EQ(decoded.remote, 0.0f);
}

TEST(BookSyncSyncedRecord, AnythingUnreadableIsNeverSynced) {
  uint8_t record[BookSync::SYNCED_RECORD_SIZE];
  BookSync::encodeSynced(syncedAt(0.5f, 0.5f), record);
  EXPECT_EQ(BookSync::decodeSynced(nullptr, sizeof(record)).kind, BookSync::SyncedRecord::Kind::None);
  EXPECT_EQ(BookSync::decodeSynced(record, sizeof(record) - 1).kind, BookSync::SyncedRecord::Kind::None);
  EXPECT_EQ(BookSync::decodeSynced(record, sizeof(record) + 1).kind, BookSync::SyncedRecord::Kind::None);

  uint8_t bad[BookSync::SYNCED_RECORD_SIZE];
  std::copy(std::begin(record), std::end(record), bad);
  bad[3] = '2';
  EXPECT_EQ(BookSync::decodeSynced(bad, sizeof(bad)).kind, BookSync::SyncedRecord::Kind::None);

  std::copy(std::begin(record), std::end(record), bad);
  bad[4] = 0x41;  // local 1,000,001 millionths
  bad[5] = 0x42;
  bad[6] = 0x0F;
  bad[7] = 0x00;
  EXPECT_EQ(BookSync::decodeSynced(bad, sizeof(bad)).kind, BookSync::SyncedRecord::Kind::None);

  std::copy(std::begin(record), std::end(record), bad);
  bad[8] = 0xFF;  // remote out of range
  bad[9] = 0xFF;
  bad[10] = 0xFF;
  bad[11] = 0xFF;
  EXPECT_EQ(BookSync::decodeSynced(bad, sizeof(bad)).kind, BookSync::SyncedRecord::Kind::None);
}
