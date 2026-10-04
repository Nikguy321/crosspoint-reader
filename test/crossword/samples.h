#pragma once

// Test-only crossword samples in the built-in text format (lib/Crossword/CwText.h), shared by
// test/crossword and test/crossword_preview and parsed with cw::parseTextPuzzle. They were the
// placeholder built-ins before the real set landed; the rule, mark and drawing tests play them so
// that no test depends on the shipped puzzles (scripts/crossword/builtin.txt), which may change.
// Original grids and clues, written for this repository.

namespace cw_samples {

// 5x5: ##HUT / #CASE / RAVEN / AGED# / PEN## (the puzzle most rule tests play; the .ipuz and .puz
// fixtures of test/crossword/make_fixtures.py hold the same grid).
inline constexpr char SHELTER[] =
    "=== sample-shelter | Shelter\n"
    "##HUT\n"
    "#CASE\n"
    "RAVEN\n"
    "AGED#\n"
    "PEN##\n"
    "A 1 Simple shelter in the woods\n"
    "A 4 Detective's puzzle to crack\n"
    "A 5 Large black bird with a croak\n"
    "A 6 Like fine cheese or wine\n"
    "A 7 Ballpoint, for one\n"
    "D 1 Safe harbor\n"
    "D 2 Secondhand\n"
    "D 3 Fingers on two hands\n"
    "D 4 Home for a pet hamster\n"
    "D 5 Knock on a door\n";

// 5x5: #PAY# / MERIT / EAGER / TRULY / #LED#.
inline constexpr char KEEN[] =
    "=== sample-keen | Keen\n"
    "#PAY#\n"
    "MERIT\n"
    "EAGER\n"
    "TRULY\n"
    "#LED#\n"
    "A 1 Settle the bill\n"
    "A 4 Deserve, as praise\n"
    "A 6 Keen to get started\n"
    "A 7 In fact; honestly\n"
    "A 8 Was in front\n"
    "D 1 Gem found inside an oyster\n"
    "D 2 Disagree out loud\n"
    "D 3 Give way at a traffic sign\n"
    "D 4 Was introduced to\n"
    "D 5 Give it a shot\n";

// 7x7 with two unchecked squares, each in a Down entry only: the A of AGED at (2,6) and the K of
// BOOK at (4,0), which no Across entry covers.
inline constexpr char OASIS[] =
    "=== sample-oasis | Oasis\n"
    "#YES###\n"
    "BEAK###\n"
    "OASIS#A\n"
    "ORE#COG\n"
    "K#LEAVE\n"
    "###GLAD\n"
    "###GEL#\n"
    "A 1 Opposite of no\n"
    "A 4 Bird's bill\n"
    "A 5 Green spot in a desert\n"
    "A 8 Rock worth mining\n"
    "A 9 Tooth on a gear wheel\n"
    "A 11 Head out the door\n"
    "A 13 Happy and pleased\n"
    "A 14 Hair-styling goo\n"
    "D 1 Twelve months\n"
    "D 2 Painter's stand\n"
    "D 3 Glide down a snowy slope\n"
    "D 4 Novel or atlas\n"
    "D 6 Bathroom weight checker\n"
    "D 7 Grown old\n"
    "D 10 Shape of a racetrack\n"
    "D 12 Breakfast item that may be scrambled\n";

}  // namespace cw_samples
