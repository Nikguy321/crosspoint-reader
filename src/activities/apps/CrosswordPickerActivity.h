#pragma once

#include <Crossword.h>

#include <memory>

#include "CrosswordStore.h"
#include "activities/UiListActivity.h"

// Crossword's puzzle picker ("Puzzles..."), pushed over the game. Two levels: the collections
// (Built-in with its solved count, On the card = the folder's root files, then each pack folder;
// an empty card says where the files go), and a collection's puzzles (built-ins: title and size;
// card files: the name without its extension), solved ones marked. It opens on the last
// collection's puzzles; Back goes up to the collections, then back to the game. Choosing a puzzle
// parses it into `staged` (the game's) and closes with a MenuResult; one that is refused stays
// in the list with its reason under it.
class CrosswordPickerActivity final : public UiListActivity {
 public:
  // prefs / staged: the game's, which stays on the stack under this screen. currentKey: the
  // puzzle being played (marked, and selected when its collection opens).
  CrosswordPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, cw::Prefs& prefs,
                          std::unique_ptr<cw::Puzzle>& staged, const char* currentKey);

  void onEnter() override;

 private:
  enum class Level : uint8_t { Collections, Puzzles };
  struct Status {
    cw::Error error = cw::Error::None;
    uint16_t w = 0;  // as the file gave it, for "17x17 - up to 15x15"
    uint16_t h = 0;
    bool solved = false;
  };

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  void showCollections();
  // Lists a collection's puzzles; false when it is no collection.
  bool showPuzzles(const char* collection);
  // Collection row -> key ("builtin", "card", "pack:<folder>").
  bool collectionAt(int index, char* out, size_t cap) const;
  bool keyAt(int index, char* out, size_t cap) const;
  void choose(int index);
  static const char* reasonFor(const Status& status, char* buf, size_t cap);

  cw::Prefs& prefs;
  std::unique_ptr<cw::Puzzle>& staged;
  char currentKey[cw::MAX_SOURCE_KEY + 1] = {};
  Level level = Level::Collections;
  char collection[cw::MAX_COLLECTION_KEY + 1] = {};  // the open one (Level::Puzzles)
  cw::store::SolvedList solved;
  // Collections: the card's packs.
  std::unique_ptr<cw::store::FileEntry[]> packs;  // cw::MAX_PACKS
  int packCount = 0;
  int rootFiles = 0;
  int builtinSolved = 0;
  // Puzzles of a card collection (a built-in collection needs no list).
  std::unique_ptr<cw::store::FileEntry[]> files;  // cw::MAX_FOLDER_FILES
  std::unique_ptr<Status[]> status;               // per puzzle row
  int puzzleCount = 0;
  bool capped = false;
  // One row's text at a time (the provider's pointers live until its next call).
  char label[cw::MAX_SOURCE_KEY + 1] = {};
  char subtitle[64] = {};
};
