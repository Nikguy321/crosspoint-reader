# Time Zone From Location. lib/TzGrid is the WiPhone repository's time zone map, copied unchanged;
# TzGridTest runs it against that repository's own vectors (vectors_tzgrid.json, copied here
# unchanged), so this copy answers what the WiPhone and COVEY answer. ClockZoneTest checks the
# clock table (src/util/TimezoneTable.cpp) and the decisions on top (src/util/ClockZone.cpp).
add_library(TzGridHost STATIC ${REPO_ROOT}/lib/TzGrid/tz_grid.cpp)
target_include_directories(TzGridHost PUBLIC ${REPO_ROOT}/lib/TzGrid)
target_compile_definitions(TzGridHost PUBLIC TZ_VECTORS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/vectors_tzgrid.json")

add_executable(TzGridTest TzGridTest.cpp)
target_link_libraries(TzGridTest PRIVATE TzGridHost crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(TzGridTest)

add_executable(ClockZoneTest
  ClockZoneTest.cpp
  ${REPO_ROOT}/src/util/ClockZone.cpp
  ${REPO_ROOT}/src/util/TimezoneTable.cpp
)
target_include_directories(ClockZoneTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ClockZoneTest PRIVATE TzGridHost crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ClockZoneTest)
