add_executable(WifiJoinOrderTest WifiJoinOrderTest.cpp)

target_include_directories(WifiJoinOrderTest PRIVATE ../../src)

target_link_libraries(WifiJoinOrderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WifiJoinOrderTest)
