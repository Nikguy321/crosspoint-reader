# Where the sleep-card location came from (src/sleepcards/LocationFix.cpp): the stored record,
# its checks, and the "From Wi-Fi, ±80 m, Sep 29" line under the Location row.
add_executable(SleepCardLocationTest SleepCardLocationTest.cpp)
target_link_libraries(SleepCardLocationTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(SleepCardLocationTest)
