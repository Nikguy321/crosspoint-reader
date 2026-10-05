# Survival guide, the pure half (lib/Guide): the pack and page parsers (with corruption), the layout
# model over a fake font, navigation, search, the state codecs. GuideTest needs nothing else.
file(GLOB GUIDE_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Guide/*.cpp)
add_executable(GuideTest GuideTest.cpp GuideLayoutTest.cpp GuideSearchTest.cpp GuideStateTest.cpp ${GUIDE_SOURCES})
target_include_directories(GuideTest PRIVATE ${REPO_ROOT}/lib/Guide)
target_compile_definitions(GuideTest PRIVATE GUIDE_PACK="${REPO_ROOT}/packs/guide/build/survival"
  GUIDE_SCRIPTS="${REPO_ROOT}/scripts/guide")
target_link_libraries(GuideTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GuideTest)

# The real device pack (packs/guide/build/survival) through the same code with the device's own
# fonts (SleepCardHost: the real GfxRenderer and built-in fonts) and the page screen's metrics
# adapter: every topic parses, every page fits in at most 3 screens, every quick card in one, the
# index finds every topic, and corruptions of every file are refused, never a crash.
add_executable(GuidePackTest GuidePackTest.cpp ${REPO_ROOT}/src/activities/apps/GuideMetrics.cpp ${GUIDE_SOURCES})
target_include_directories(GuidePackTest PRIVATE ${REPO_ROOT}/lib/Guide)
target_compile_definitions(GuidePackTest PRIVATE GUIDE_PACK="${REPO_ROOT}/packs/guide/build/survival"
  GUIDE_SCRIPTS="${REPO_ROOT}/scripts/guide")
target_link_libraries(GuidePackTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(GuidePackTest)

# The committed device pack must be what the builder makes from the source (and the source must
# pass the validator, which make_pack runs first). --text-only: the figures are checked for presence
# and against SHA256SUMS but not re-rendered, because a fresh render's pixels depend on the Pillow
# that makes them; `make_pack.py --check` (no flag) re-renders and compares them, before a commit.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME guide_pack_current COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/guide/make_pack.py --check --text-only)
endif()
