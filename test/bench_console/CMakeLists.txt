add_executable(BenchConsoleTest
  BenchConsoleTest.cpp
  ${REPO_ROOT}/lib/BenchConsole/BenchProtocol.cpp
  ${REPO_ROOT}/lib/BenchConsole/BenchInjection.cpp
)

target_include_directories(BenchConsoleTest PRIVATE
  ${REPO_ROOT}/lib/BenchConsole
)

target_link_libraries(BenchConsoleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BenchConsoleTest)
