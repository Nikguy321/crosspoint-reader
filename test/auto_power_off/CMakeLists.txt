add_executable(AutoPowerOffTest AutoPowerOffTest.cpp)

target_include_directories(AutoPowerOffTest PRIVATE ../../src)

target_link_libraries(AutoPowerOffTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AutoPowerOffTest)
