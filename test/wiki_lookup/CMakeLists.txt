# Apps > AI's Wikipedia lookup (lib/WikiLookup, shared with the WiPhone firmware): the keywords
# (contractions, the follow-up's borrowed words), the URL, the response, relevance, the background
# and the sources line. WikiLookupTest is the WiPhone's tests/test_wiki.cpp on gtest; the X4's own
# additions (the accent fold, the shared vectors) are at its end. Links GeminiHost (test/gemini).
add_executable(WikiLookupTest WikiLookupTest.cpp)
target_link_libraries(WikiLookupTest PRIVATE GeminiHost crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WikiLookupTest)
