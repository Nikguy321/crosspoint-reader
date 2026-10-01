add_executable(LiveSleepTest LiveSleepTest.cpp)

target_include_directories(LiveSleepTest PRIVATE ../../src)

target_link_libraries(LiveSleepTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LiveSleepTest)
