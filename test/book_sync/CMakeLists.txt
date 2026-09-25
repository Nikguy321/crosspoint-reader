add_executable(BookSyncTest
  BookSyncTest.cpp
  ${REPO_ROOT}/lib/BookSync/BookSyncConfig.cpp
  ${REPO_ROOT}/lib/BookSync/BookSyncLanding.cpp
  ${REPO_ROOT}/lib/BookSync/BookSyncPatience.cpp
  ${REPO_ROOT}/lib/BookSync/BookSyncPush.cpp
)

target_include_directories(BookSyncTest PRIVATE
  ${REPO_ROOT}/lib/BookSync
)

target_link_libraries(BookSyncTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookSyncTest)
