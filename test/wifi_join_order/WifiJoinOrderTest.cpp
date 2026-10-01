#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "network/WifiJoinOrder.h"

using namespace wifi_join;

namespace {
std::vector<std::string> ordered(const std::vector<Seen>& seen, std::string_view last,
                                 const std::vector<std::string_view>& excluded = {}, size_t cap = 8) {
  std::vector<uint16_t> out(cap);
  const size_t n = order(seen.data(), seen.size(), last, excluded.data(), excluded.size(), out.data(), cap);
  std::vector<std::string> names;
  for (size_t i = 0; i < n; i++) names.emplace_back(seen[out[i]].ssid);
  return names;
}
using Names = std::vector<std::string>;
}  // namespace

TEST(WifiJoinOrder, TheLastNetworkFirstWhenItIsInView) {
  const std::vector<Seen> seen = {{"Cafe", -40, true}, {"Home", -80, true}, {"Work", -55, true}};
  EXPECT_EQ(ordered(seen, "Home"), (Names{"Home", "Cafe", "Work"}));
}

TEST(WifiJoinOrder, OtherwiseTheNextBestSavedNetworkInView) {
  // The last network is not in view: no blind try at it, the strongest saved one goes first.
  const std::vector<Seen> seen = {{"Work", -70, true}, {"Library", -50, true}, {"Neighbour", -30, false}};
  EXPECT_EQ(ordered(seen, "Home"), (Names{"Library", "Work"}));
  EXPECT_EQ(ordered(seen, ""), (Names{"Library", "Work"}));
}

TEST(WifiJoinOrder, UnsavedHiddenAndExcludedNetworksAreNeverCandidates) {
  const std::vector<Seen> seen = {
      {"", -20, true}, {"Open", -30, false}, {"Peer", -35, true}, {"Hub", -40, true}, {"Home", -60, true}};
  EXPECT_EQ(ordered(seen, "Peer", {"Peer", "Hub"}), (Names{"Home"}));
  // An empty exclusion (no peer configured) excludes nothing.
  EXPECT_EQ(ordered(seen, "", {"", ""}), (Names{"Peer", "Hub", "Home"}));
}

TEST(WifiJoinOrder, OneEntryPerNetworkAtItsStrongest) {
  // Two access points of one network: it ranks by the stronger, listed once.
  const std::vector<Seen> seen = {{"Mesh", -85, true}, {"Work", -60, true}, {"Mesh", -45, true}};
  std::vector<uint16_t> out(8);
  const size_t n = order(seen.data(), seen.size(), "", nullptr, 0, out.data(), out.size());
  ASSERT_EQ(n, 2u);
  EXPECT_EQ(out[0], 2);  // the -45 sighting (its channel and BSSID are the ones to join)
  EXPECT_EQ(out[1], 1);
  // The last network keeps the lead even when its first sighting was weak.
  EXPECT_EQ(ordered(seen, "Mesh"), (Names{"Mesh", "Work"}));
}

TEST(WifiJoinOrder, CapKeepsTheBest) {
  std::vector<Seen> seen;
  const char* names[] = {"a", "b", "c", "d", "e"};
  for (int i = 0; i < 5; i++) seen.push_back({names[i], -90 + i * 10, true});  // e strongest
  EXPECT_EQ(ordered(seen, "", {}, 3), (Names{"e", "d", "c"}));
  EXPECT_EQ(ordered(seen, "a", {}, 3), (Names{"a", "e", "d"}));
  // Ties keep scan order.
  const std::vector<Seen> tied = {{"x", -50, true}, {"y", -50, true}, {"z", -50, true}};
  EXPECT_EQ(ordered(tied, ""), (Names{"x", "y", "z"}));
}

TEST(WifiJoinOrder, NothingSavedInViewIsEmpty) {
  const std::vector<Seen> seen = {{"Neighbour", -40, false}};
  EXPECT_TRUE(ordered(seen, "Home").empty());
  EXPECT_TRUE(ordered({}, "Home").empty());
  uint16_t out[1];
  EXPECT_EQ(order(nullptr, 3, "", nullptr, 0, out, 1), 0u);
  EXPECT_EQ(order(seen.data(), 1, "", nullptr, 0, out, 0), 0u);
}

TEST(WifiJoinOrder, AHiddenLastNetworkIsTriedBlindOnlyWhenAHiddenOneWasSeen) {
  EXPECT_TRUE(tryHiddenLast(true, false, true));
  EXPECT_FALSE(tryHiddenLast(true, false, false));  // nothing hidden in view: it is just not here
  EXPECT_FALSE(tryHiddenLast(true, true, true));    // in view: already tried in order
  EXPECT_FALSE(tryHiddenLast(false, false, true));  // not saved (forgotten)
}
