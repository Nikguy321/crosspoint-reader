# Sudoku drawn with the real renderer and fonts: SleepCardHost (test/sleep_card_preview) brings
# GfxRenderer, the built-in fonts, sleepcards::draw, the i18n tables and the PNG writer. Writes
# build/sudoku/*.png.
file(GLOB SUDOKU_PREVIEW_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Sudoku/*.cpp)
add_executable(SudokuPreviewTest
  SudokuPreviewTest.cpp
  ${REPO_ROOT}/src/activities/apps/SudokuDraw.cpp
  ${REPO_ROOT}/src/activities/apps/AppDraw.cpp
  ${SUDOKU_PREVIEW_SOURCES}
)
target_include_directories(SudokuPreviewTest PRIVATE ${REPO_ROOT}/lib/Sudoku)
target_link_libraries(SudokuPreviewTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(SudokuPreviewTest)
