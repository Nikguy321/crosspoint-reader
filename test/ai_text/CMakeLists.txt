# Apps > AI's own pure helpers (src/activities/apps/AiText): the answer page in the survival guide's
# page format - checked by parsing and laying it out with lib/Guide over the guide's fake font - the
# key file's names and folder, the date's zone word and the clock's UTC offset.
file(GLOB AI_TEXT_GUIDE_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Guide/*.cpp)
add_executable(AiTextTest AiTextTest.cpp ${REPO_ROOT}/src/activities/apps/AiText.cpp ${AI_TEXT_GUIDE_SOURCES})
target_include_directories(AiTextTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/Guide ${REPO_ROOT}/test/guide)
target_link_libraries(AiTextTest PRIVATE GeminiHost crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(AiTextTest)

# The answer page drawn with the real renderer and fonts (SleepCardHost) through the survival guide's
# layout and drawing, as AiPageActivity draws it: build/ai/*.png, the text column and bezels checked.
add_executable(AiPreviewTest
  AiPreviewTest.cpp
  ${REPO_ROOT}/src/activities/apps/AiText.cpp
  ${REPO_ROOT}/src/activities/apps/GuideDraw.cpp
  ${REPO_ROOT}/src/activities/apps/GuideMetrics.cpp
  ${AI_TEXT_GUIDE_SOURCES}
)
target_include_directories(AiPreviewTest PRIVATE ${REPO_ROOT}/lib/Guide ${REPO_ROOT}/test/guide)
target_link_libraries(AiPreviewTest PRIVATE SleepCardHost GeminiHost GTest::gtest_main)
gtest_discover_tests(AiPreviewTest)
