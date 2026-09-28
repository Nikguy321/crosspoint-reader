# Day card: its pure helpers and its rendering. SleepCardHost (test/sleep_card_preview) brings the
# real renderer, the fonts, every src/sleepcards file and the sample context (CardPreview.h).
add_executable(SleepCardDayTest SleepCardDayTest.cpp)
target_link_libraries(SleepCardDayTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(SleepCardDayTest)
