# Locate Me's phone source: NMEA from a phone on its own hotspot and the gates a fix must pass
# (src/network/PhoneNmea.cpp, pure; the socket half, network/PhoneGps, is device-only).
add_executable(PhoneNmeaTest
  PhoneNmeaTest.cpp
  ${REPO_ROOT}/src/network/PhoneNmea.cpp
)

target_include_directories(PhoneNmeaTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(PhoneNmeaTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PhoneNmeaTest)
