# Weather card: what it shows from a cache record, its units, and Shuffle's check. SleepCardHost
# (test/sleep_card_preview) brings the renderer, the fonts, every src/sleepcards file and the sample
# context; the drawing itself is previewed there (build/cards/weather*.png).
add_executable(SleepCardWeatherTest SleepCardWeatherTest.cpp)
target_link_libraries(SleepCardWeatherTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(SleepCardWeatherTest)
