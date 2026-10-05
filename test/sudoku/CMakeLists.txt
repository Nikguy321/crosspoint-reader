# Sudoku's pure half: the engine (counter, ladder, generator, hints), the play model and undo ring,
# the save codecs, the layout, the touch tracker and the notes digits.
file(GLOB SUDOKU_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Sudoku/*.cpp)
add_executable(SudokuTest SudokuEngineTest.cpp SudokuModelTest.cpp SudokuSaveTest.cpp SudokuLayoutTest.cpp
  ${SUDOKU_SOURCES})
target_include_directories(SudokuTest PRIVATE ${REPO_ROOT}/lib/Sudoku)
target_link_libraries(SudokuTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SudokuTest)
