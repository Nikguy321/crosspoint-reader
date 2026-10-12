#include "network/WebPathGuard.h"

#include <HalStorage.h>
#include <gtest/gtest.h>

namespace {

// The reader's card as File Transfer sees it: its own hidden folder (settings, Wi-Fi and KOSync
// logins), the AI key folder, books with a hidden folder among them, and a file at the root.
class WebPathGuard : public ::testing::Test {
 protected:
  void SetUp() override {
    using fakecard::add;
    fakecard::reset();
    auto cp = add(fakecard::root, ".crosspoint", "CROSSP~1", true);
    add(cp, "koreader.json", "KOREAD~1.JSO", false);
    add(cp, "settings.json", "SETTIN~1.JSO", false);
    // Made on the reader (x4bench put: Storage.mkdir): SdFat turns the space into '_'
    auto keys = add(fakecard::root, "API Keys", "API_KE~1", true);
    add(keys, "gemini.txt", "GEMINI.TXT", false);
    auto books = add(fakecard::root, "Books", "BOOKS", true);
    book = add(books, "Abaddon's Gate.epub", "ABADDO~1.EPU", false);
    auto hidden = add(books, ".hidden", "HIDDEN~1", true);
    add(hidden, "x.epub", "X.EPU", false);
    add(fakecard::root, "sleep.log", "SLEEP.LOG", false);
  }

  std::shared_ptr<fakecard::Entry> book;
};

}  // namespace

// The fake opens what SdFat opens, so the alias cases below are real ways in
TEST_F(WebPathGuard, FakeCardOpensLikeSdFat) {
  EXPECT_TRUE(Storage.open("/CROSSP~1/koreader.json"));
  EXPECT_TRUE(Storage.open("/crossp~1/KOREAD~1.JSO"));
  EXPECT_TRUE(Storage.open("/ .crosspoint./koreader.json"));
  EXPECT_TRUE(Storage.open("/API_KE~1/GEMINI.TXT"));
  EXPECT_TRUE(Storage.open("/books/HIDDEN~1/x.epub"));
  EXPECT_FALSE(Storage.open("/Books/./x.epub"));
  EXPECT_FALSE(Storage.open("/CROSSPOINT/koreader.json"));
}

// The trap: opened by its alias, a folder gives back the alias - no dot to see - so the guard
// cannot ask the opened file its name; storedName() reads the long name the card stored
TEST_F(WebPathGuard, AnAliasOpenGivesBackTheAlias) {
  char name[32];
  HalFile f = Storage.open("/CROSSP~1");
  ASSERT_TRUE(f);
  ASSERT_GT(f.getName(name, sizeof(name)), 0u);
  EXPECT_STREQ(name, "CROSSP~1");
  EXPECT_EQ(Storage.storedName("/CROSSP~1", name, sizeof(name)), 11);
  EXPECT_STREQ(name, ".crosspoint");
  EXPECT_EQ(Storage.storedName("/nothing", name, sizeof(name)), 0);
}

TEST_F(WebPathGuard, OrdinaryPathsPass) {
  EXPECT_FALSE(isProtectedWebPath("/"));
  EXPECT_FALSE(isProtectedWebPath("/Books"));
  EXPECT_FALSE(isProtectedWebPath("/Books/Abaddon's Gate.epub"));
  EXPECT_FALSE(isProtectedWebPath("/BOOKS/ABADDO~1.EPU"));  // an ordinary file's alias is still ordinary
  EXPECT_FALSE(isProtectedWebPath("/sleep.log"));
  // Not on the card yet (an upload, a new folder): read as typed
  EXPECT_FALSE(isProtectedWebPath("/Books/new.epub"));
  EXPECT_FALSE(isProtectedWebPath("/New Folder/x.epub"));
}

TEST_F(WebPathGuard, HiddenFoldersAsTyped) {
  EXPECT_TRUE(isProtectedWebPath("/.crosspoint"));
  EXPECT_TRUE(isProtectedWebPath("/.crosspoint/koreader.json"));
  EXPECT_TRUE(isProtectedWebPath("/ .crosspoint/koreader.json"));
  EXPECT_TRUE(isProtectedWebPath("/.crosspoint./koreader.json"));
  EXPECT_TRUE(isProtectedWebPath("/Books/.hidden/x.epub"));
  EXPECT_TRUE(isProtectedWebPath("/Books/.hidden"));
  EXPECT_TRUE(isProtectedWebPath("/New Folder/.x"));
  EXPECT_TRUE(isProtectedWebPath("/.new/x.epub"));
}

TEST_F(WebPathGuard, HiddenFoldersByAlias) {
  EXPECT_TRUE(isProtectedWebPath("/CROSSP~1"));
  EXPECT_TRUE(isProtectedWebPath("/CROSSP~1/koreader.json"));
  EXPECT_TRUE(isProtectedWebPath("/crossp~1/koreader.json"));
  EXPECT_TRUE(isProtectedWebPath("/CROSSP~1/KOREAD~1.JSO"));
  EXPECT_TRUE(isProtectedWebPath("/CROSSP~1/new.json"));  // an upload into it
  EXPECT_TRUE(isProtectedWebPath("/Books/HIDDEN~1/x.epub"));
  EXPECT_TRUE(isProtectedWebPath("/BOOKS/HIDDEN~1"));
  EXPECT_FALSE(isProtectedWebPath("/CROSSP~2/x"));  // no such folder: nothing to reach
}

TEST_F(WebPathGuard, KeyFolderMadeOnAComputer) {
  fakecard::reset();
  auto keys = fakecard::add(fakecard::root, "API Keys", "APIKEY~1", true);
  fakecard::add(keys, "gemini.txt", "GEMINI.TXT", false);
  EXPECT_TRUE(isProtectedWebPath("/APIKEY~1/gemini.txt"));
  EXPECT_TRUE(isProtectedWebPath("/apikey~1"));
}

TEST_F(WebPathGuard, KeyFolderAnySpelling) {
  EXPECT_TRUE(isProtectedWebPath("/API Keys"));
  EXPECT_TRUE(isProtectedWebPath("/API Keys/gemini.txt"));
  EXPECT_TRUE(isProtectedWebPath("/api keys/gemini.txt"));
  EXPECT_TRUE(isProtectedWebPath("/API Keys./gemini.txt"));
  EXPECT_TRUE(isProtectedWebPath("/API_KE~1/gemini.txt"));
  EXPECT_TRUE(isProtectedWebPath("/api_ke~1"));
  EXPECT_TRUE(isProtectedWebPath("/API_KE~1/GEMINI.TXT"));
  EXPECT_TRUE(isProtectedWebPath("/Books/API Keys"));  // a new one anywhere is refused too
}

// A stored name the card cannot give back could be anything: refused
TEST_F(WebPathGuard, DamagedNameRefused) {
  book->damagedName = true;
  EXPECT_TRUE(isProtectedWebPath("/Books/Abaddon's Gate.epub"));
  EXPECT_FALSE(isProtectedWebPath("/Books"));
}

// A name too long for a small buffer still gets read: SdFat gives nothing for one that does not fit
TEST_F(WebPathGuard, LongestNamesRead) {
  const std::string longHidden = "." + std::string(254, 'a');
  fakecard::add(fakecard::root, longHidden, "AAAAAA~1", true);
  EXPECT_TRUE(isProtectedWebPath("/AAAAAA~1/x"));
  const std::string longName(255, 'b');
  fakecard::add(fakecard::root, longName, "BBBBBB~1", true);
  EXPECT_FALSE(isProtectedWebPath("/BBBBBB~1"));
}
