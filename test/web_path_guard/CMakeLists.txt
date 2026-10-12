# What the web File Transfer server and WebDAV never touch (src/network/WebPathGuard): hidden names
# and the AI app's key folder, by any spelling the card opens them by - over a fake FAT card
# (stubs/HalStorage.h) that resolves names as SdFat does, 8.3 aliases included.
add_executable(WebPathGuardTest
  WebPathGuardTest.cpp
  ${REPO_ROOT}/src/network/WebPathGuard.cpp
  ${REPO_ROOT}/src/activities/apps/AiText.cpp
  ${REPO_ROOT}/src/util/CardPath.cpp
)
target_include_directories(WebPathGuardTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(WebPathGuardTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WebPathGuardTest)
