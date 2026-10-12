# Card paths read as the card's library (SdFat) reads them: src/util/CardPath.
add_executable(CardPathTest CardPathTest.cpp ${REPO_ROOT}/src/util/CardPath.cpp)
target_include_directories(CardPathTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(CardPathTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CardPathTest)
