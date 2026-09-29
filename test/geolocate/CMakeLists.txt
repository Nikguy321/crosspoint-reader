# Locate Me: the geolocate request builder, both services' response parsers and the choice
# between them (src/network/GeolocateProtocol.cpp, pure; the HTTPS half is device-only).
add_executable(GeolocateProtocolTest
  GeolocateProtocolTest.cpp
  ${REPO_ROOT}/src/network/GeolocateProtocol.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(GeolocateProtocolTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(GeolocateProtocolTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GeolocateProtocolTest)
