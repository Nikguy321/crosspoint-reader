# The real SDK FrontlightManager and firmware HalFrontlight against host
# stand-ins (host/): duties and the light-sleep XTAL hold are observed at the
# fake LEDC.
add_executable(FrontlightPwmTest
  FrontlightPwmTest.cpp
  host/FakeLedc.cpp
  ${REPO_ROOT}/freeink-sdk/libs/hardware/FrontlightManager/src/FrontlightManager.cpp
  ${REPO_ROOT}/lib/hal/HalFrontlight.cpp
)

# host/ first: its stand-ins shadow Arduino, BoardConfig, IDF and Logging.
target_include_directories(FrontlightPwmTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/host
  ${REPO_ROOT}/freeink-sdk/libs/hardware/FrontlightManager/include
  ${REPO_ROOT}/lib/hal
)

target_compile_definitions(FrontlightPwmTest PRIVATE
  BOARDCONFIG_H_PATH="${REPO_ROOT}/freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h"
)

target_link_libraries(FrontlightPwmTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FrontlightPwmTest)
