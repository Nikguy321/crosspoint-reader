# Crossword drawn with the real renderer and fonts: SleepCardHost (test/sleep_card_preview)
# brings GfxRenderer, the built-in fonts, sleepcards::draw, the i18n tables and the PNG writer.
# Writes build/crossword/*.png. The .ipuz fixtures need ArduinoJson (fetched by
# test/crossword, header-only).
include(FetchContent)
FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR header-only
)
FetchContent_MakeAvailable(arduinojson)

file(GLOB CROSSWORD_PREVIEW_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Crossword/*.cpp)
add_executable(CrosswordPreviewTest
  CrosswordPreviewTest.cpp
  ${REPO_ROOT}/src/activities/apps/CrosswordDraw.cpp
  ${REPO_ROOT}/src/activities/apps/AppDraw.cpp
  ${CROSSWORD_PREVIEW_SOURCES}
)
target_include_directories(CrosswordPreviewTest PRIVATE
  ${REPO_ROOT}/lib/Crossword
  ${REPO_ROOT}/test/crossword  # samples.h: the test-only puzzles
  ${arduinojson_SOURCE_DIR}/src
)
target_compile_definitions(CrosswordPreviewTest PRIVATE
  CROSSWORD_FIXTURES="${REPO_ROOT}/test/crossword/fixtures")
target_link_libraries(CrosswordPreviewTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(CrosswordPreviewTest)
