add_executable(PowerPolicyTest PowerPolicyTest.cpp)

target_link_libraries(PowerPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PowerPolicyTest)
