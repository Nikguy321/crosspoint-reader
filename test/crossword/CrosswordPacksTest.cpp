// The downloadable crossword packs (packs/crossword/<pack>/*.ipuz, written by
// scripts/crossword/make_packs.py from each pack's source.txt) through the firmware's own .ipuz
// reader: every file loads whole, its size is the one its name gives, every entry has a clue, and
// no two pack puzzles (or a pack puzzle and a built-in) share a solution fingerprint.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Crossword.h"

using namespace cw;
namespace fs = std::filesystem;

namespace {

struct PackFile {
  std::string pack;  // the folder name: the pack's name on the reader
  std::string name;  // "mini-031.ipuz"
  fs::path path;
};

std::vector<PackFile> packFiles() {
  std::vector<PackFile> out;
  const fs::path root(CROSSWORD_PACKS);
  if (!fs::is_directory(root)) return out;
  for (const auto& folder : fs::directory_iterator(root)) {
    if (!folder.is_directory()) continue;
    for (const auto& file : fs::directory_iterator(folder.path())) {
      const std::string name = file.path().filename().string();
      if (file.is_regular_file() && formatOf(name.c_str()) == Format::Ipuz) {
        out.push_back({folder.path().filename().string(), name, file.path()});
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const PackFile& a, const PackFile& b) { return a.path < b.path; });
  return out;
}

std::string readAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

// The side a pack puzzle's id names: mini = 5, midi = 7, maxi = 9 (0 = none).
int sideOf(const std::string& id) {
  if (id.rfind("mini-", 0) == 0) return 5;
  if (id.rfind("midi-", 0) == 0) return 7;
  if (id.rfind("maxi-", 0) == 0) return 9;
  return 0;
}

}  // namespace

TEST(CrosswordPacks, EveryPackPuzzleLoadsThroughTheIpuzReader) {
  const std::vector<PackFile> files = packFiles();
  ASSERT_FALSE(files.empty()) << "no .ipuz under " << CROSSWORD_PACKS;

  std::map<uint32_t, std::string> owners;  // fnv -> who has it
  auto p = std::make_unique<Puzzle>();
  for (size_t i = 0; i < builtinCount(); i++) {
    const BuiltinPuzzle& b = builtinPuzzle(i);
    owners.emplace(b.fnv, std::string("builtin:") + b.id);
  }

  std::map<std::string, int> perPack;
  int clues = 0;
  for (const PackFile& f : files) {
    SCOPED_TRACE(f.pack + "/" + f.name);
    perPack[f.pack]++;
    EXPECT_TRUE(isPackFolder(f.pack.c_str()));
    // The key the reader keeps for this file must fit (the store skips longer ones).
    const std::string key = std::string(CARD_DIR) + "/" + f.pack + "/" + f.name;
    EXPECT_LE(key.size(), MAX_SOURCE_KEY);

    const std::string data = readAll(f.path);
    EXPECT_LE(data.size(), MAX_IPUZ_BYTES);
    MemoryReader in(data.data(), data.size());
    const LoadStatus st = parseIpuz(in, key.c_str(), *p);
    ASSERT_EQ(st.error, Error::None);

    EXPECT_STREQ(p->sourceKey, key.c_str());
    EXPECT_EQ(p->fnv, puzzleFnv(p->w, p->h, p->solution));
    EXPECT_GT(std::strlen(p->title), 0u);
    EXPECT_STREQ(p->author, "CrossPoint Reader fork");
    EXPECT_STREQ(p->copyright, "Original puzzle - MIT License");

    // The size its name gives, square.
    const std::string id = f.name.substr(0, f.name.size() - std::strlen(".ipuz"));
    const int side = sideOf(id);
    EXPECT_NE(side, 0) << "an id names its size (mini-/midi-/maxi-)";
    EXPECT_EQ(p->w, side);
    EXPECT_EQ(p->h, side);

    // Every entry has its clue (the reader refuses a missing one; this checks none is empty) and
    // every square is checked Across and Down, as in the built-ins.
    ASSERT_GT(p->entryCount, 0);
    for (int e = 0; e < p->entryCount; e++) {
      EXPECT_GT(p->entries[e].clueLen, 0) << "entry " << e;
      EXPECT_LE(p->entries[e].clueLen, 160);
      EXPECT_GE(p->entries[e].len, 3);
      for (const char* c = p->clue(e); *c; c++) EXPECT_TRUE(*c >= 0x20 && *c < 0x7F) << "ASCII clues";
    }
    for (int c = 0; c < p->cells(); c++) {
      if (!p->isBlock(c)) EXPECT_TRUE(p->entryAt[ACROSS][c] != NO_ENTRY && p->entryAt[DOWN][c] != NO_ENTRY) << c;
    }
    clues += p->entryCount;

    const auto [it, fresh] = owners.emplace(p->fnv, f.pack + "/" + f.name);
    EXPECT_TRUE(fresh) << "same solution as " << it->second;
  }

  // The reader lists at most MAX_FOLDER_FILES puzzles a folder and MAX_PACKS packs.
  EXPECT_LE(perPack.size(), static_cast<size_t>(MAX_PACKS));
  for (const auto& [pack, count] : perPack) EXPECT_LE(count, MAX_FOLDER_FILES) << pack;
  std::printf("[packs] %zu puzzles, %d clues in %zu packs\n", files.size(), clues, perPack.size());
}
