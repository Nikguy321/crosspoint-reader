add_executable(SleepLedgerTest SleepLedgerTest.cpp)

target_include_directories(SleepLedgerTest PRIVATE ../../src)

target_link_libraries(SleepLedgerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepLedgerTest)
