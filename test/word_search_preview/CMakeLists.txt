# Word Search drawn with the real renderer and fonts: SleepCardHost (test/sleep_card_preview)
# brings GfxRenderer, the built-in fonts, sleepcards::draw and the PNG writer. Writes
# build/wordsearch/*.png.
file(GLOB WORD_SEARCH_PREVIEW_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/WordSearch/*.cpp)
add_executable(WordSearchPreviewTest
  WordSearchPreviewTest.cpp
  ${REPO_ROOT}/src/activities/apps/WordSearchDraw.cpp
  ${REPO_ROOT}/src/activities/apps/AppDraw.cpp
  ${WORD_SEARCH_PREVIEW_SOURCES}
)
target_include_directories(WordSearchPreviewTest PRIVATE ${REPO_ROOT}/lib/WordSearch)
target_link_libraries(WordSearchPreviewTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(WordSearchPreviewTest)
