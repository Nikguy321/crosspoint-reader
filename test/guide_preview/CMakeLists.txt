# The survival guide drawn with the real renderer and fonts (SleepCardHost: GfxRenderer, the built-in
# fonts, the i18n tables and the PNG writer) from the real device pack (packs/guide/build/survival):
# the home, a category, a figure page, a page of steps with a WARNING, a quick card, search results,
# bookmarks, About, the no-pack page, the guide menu and the full-screen figure. Writes
# build/guide/*.png. The pack's 1-bit figures are decoded here with zlib (the device uses PNGdec).
find_package(ZLIB REQUIRED)
file(GLOB GUIDE_PREVIEW_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Guide/*.cpp)
add_executable(GuidePreviewTest
  GuidePreviewTest.cpp
  ${REPO_ROOT}/src/activities/apps/GuideDraw.cpp
  ${REPO_ROOT}/src/activities/apps/GuideListModel.cpp
  ${REPO_ROOT}/src/activities/apps/GuideMetrics.cpp
  ${GUIDE_PREVIEW_SOURCES}
)
target_include_directories(GuidePreviewTest PRIVATE ${REPO_ROOT}/lib/Guide ${REPO_ROOT}/test/guide)
target_compile_definitions(GuidePreviewTest PRIVATE GUIDE_PACK="${REPO_ROOT}/packs/guide/build/survival")
target_link_libraries(GuidePreviewTest PRIVATE SleepCardHost ZLIB::ZLIB GTest::gtest_main)
gtest_discover_tests(GuidePreviewTest)
