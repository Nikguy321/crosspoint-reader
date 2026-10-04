# Crossword's pure half: model, numbering, the text / .ipuz / .puz readers, the play rules, the
# keyboard, the layout, the save codecs and the built-ins. The .ipuz reader streams through
# ArduinoJson: the firmware's lib_deps version, fetched here header-only (its own CMake project
# is not added: SOURCE_SUBDIR names no CMakeLists).
include(FetchContent)
FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR header-only
)
FetchContent_MakeAvailable(arduinojson)

file(GLOB CROSSWORD_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Crossword/*.cpp)
add_executable(CrosswordTest CrosswordTest.cpp CrosswordContactTest.cpp ${CROSSWORD_SOURCES})
target_include_directories(CrosswordTest PRIVATE
  ${REPO_ROOT}/lib/Crossword
  ${REPO_ROOT}/lib/EpdFont
  ${arduinojson_SOURCE_DIR}/src
)
target_compile_definitions(CrosswordTest PRIVATE CROSSWORD_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(CrosswordTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CrosswordTest)

# The generated built-in table and the committed fixtures must match their scripts.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME crossword_builtin_current
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/crossword/gen_builtin.py --check)
  add_test(NAME crossword_fixtures_current
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_fixtures.py --check)
endif()
