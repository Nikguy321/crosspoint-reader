# Word Search's pure half: generator, themes, selection, layout and save codec, and the theme packs.
file(GLOB WORD_SEARCH_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/WordSearch/*.cpp)
add_executable(WordSearchTest WordSearchTest.cpp ${WORD_SEARCH_SOURCES})
target_include_directories(WordSearchTest PRIVATE ${REPO_ROOT}/lib/WordSearch)
target_compile_definitions(WordSearchTest PRIVATE WORD_SEARCH_PACKS="${REPO_ROOT}/packs/wordsearch")
target_link_libraries(WordSearchTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WordSearchTest)
