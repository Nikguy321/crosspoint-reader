add_executable(PowerLedgerTest PowerLedgerTest.cpp)

target_include_directories(PowerLedgerTest PRIVATE ../../src)

target_link_libraries(PowerLedgerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PowerLedgerTest)
