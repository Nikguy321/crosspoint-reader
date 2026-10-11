# Apps > AI: the pure half shared with the WiPhone firmware (lib/Gemini: the key file, the request
# body, Google's answers, the screen's ASCII, the model ladder with its daily limits and "Retry if
# it times out", the topic). GeminiTest is the WiPhone's tests/test_gemini.cpp on gtest, less its
# checks against the phone's own fonts, with the X4's device sentence.
add_library(GeminiHost STATIC
  ${REPO_ROOT}/lib/Gemini/gemini.cpp
  ${REPO_ROOT}/lib/Gemini/json_read.cpp
  ${REPO_ROOT}/lib/WikiLookup/wiki_lookup.cpp
)
target_include_directories(GeminiHost PUBLIC ${REPO_ROOT}/lib/Gemini ${REPO_ROOT}/lib/WikiLookup)

add_executable(GeminiTest GeminiTest.cpp)
target_link_libraries(GeminiTest PRIVATE GeminiHost crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GeminiTest)

# The AI app's pinned TLS roots (src/network/AiRoots.h): every PEM against the SHA-256 written above it.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME check_ai_roots COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/check_ai_roots.py)
endif()
