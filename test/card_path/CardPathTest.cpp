#include "util/CardPath.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

std::vector<std::string> names(const char* path) {
  std::vector<std::string> out;
  const char* name = nullptr;
  size_t len = 0;
  while (cardpath::nextName(path, name, len)) out.emplace_back(name, len);
  return out;
}

}  // namespace

TEST(CardPath, NamesAsTheCardReadsThem) {
  EXPECT_EQ(names("/Books/Abaddon's Gate.epub"), (std::vector<std::string>{"Books", "Abaddon's Gate.epub"}));
  // SdFat skips leading spaces and drops trailing dots and spaces
  EXPECT_EQ(names("/ API Keys /gemini.txt"), (std::vector<std::string>{"API Keys", "gemini.txt"}));
  EXPECT_EQ(names("/ .crosspoint./koreader.json"), (std::vector<std::string>{".crosspoint", "koreader.json"}));
  // SdFat skips a name of only spaces and cannot open one of only dots: either way it is read past;
  // doubled slashes too
  EXPECT_EQ(names("//Books/./ /../x.epub"), (std::vector<std::string>{"Books", "x.epub"}));
  EXPECT_TRUE(names("/").empty());
  EXPECT_TRUE(names("").empty());
}

TEST(CardPath, HiddenName) {
  EXPECT_TRUE(cardpath::isHiddenName(".crosspoint", 11));
  EXPECT_TRUE(cardpath::isHiddenName(".x", 2));
  EXPECT_FALSE(cardpath::isHiddenName("x.y", 3));
  EXPECT_FALSE(cardpath::isHiddenName(".crosspoint", 0));
  EXPECT_FALSE(cardpath::isHiddenName(nullptr, 0));
}

TEST(CardPath, PathHasHiddenName) {
  // The reader's own folder: settings, saved Wi-Fi networks and the KOSync login
  EXPECT_TRUE(cardpath::pathHasHiddenName("/.crosspoint/koreader.json"));
  EXPECT_TRUE(cardpath::pathHasHiddenName("/.crosspoint"));
  EXPECT_TRUE(cardpath::pathHasHiddenName(".crosspoint/koreader.json"));
  // Spellings the card opens it by
  EXPECT_TRUE(cardpath::pathHasHiddenName("/ .crosspoint/koreader.json"));
  EXPECT_TRUE(cardpath::pathHasHiddenName("/.crosspoint./koreader.json"));
  EXPECT_TRUE(cardpath::pathHasHiddenName("/.crosspoint /koreader.json"));
  EXPECT_TRUE(cardpath::pathHasHiddenName("/..crosspoint/x"));
  // Anywhere in the path, the last name too
  EXPECT_TRUE(cardpath::pathHasHiddenName("/Books/.hidden/x.epub"));
  EXPECT_TRUE(cardpath::pathHasHiddenName("/Books/.DS_Store"));
  EXPECT_TRUE(cardpath::pathHasHiddenName("/Books/._x.epub"));

  EXPECT_FALSE(cardpath::pathHasHiddenName("/Books/Abaddon's Gate.epub"));
  EXPECT_FALSE(cardpath::pathHasHiddenName("/Books/My.Book.v2.epub"));
  EXPECT_FALSE(cardpath::pathHasHiddenName("/Books/book."));
  EXPECT_FALSE(cardpath::pathHasHiddenName("/Books/./x.epub"));
  EXPECT_FALSE(cardpath::pathHasHiddenName("/Books/../x.epub"));
  EXPECT_FALSE(cardpath::pathHasHiddenName("/sleep.log"));
  EXPECT_FALSE(cardpath::pathHasHiddenName("/"));
  EXPECT_FALSE(cardpath::pathHasHiddenName(""));
  EXPECT_FALSE(cardpath::pathHasHiddenName(nullptr));
}
