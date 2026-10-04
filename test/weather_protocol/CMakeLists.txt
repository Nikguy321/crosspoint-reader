# The Weather card's pure half: request URLs, the Open-Meteo and NWS parsers, the fetch policy
# (src/network/WeatherProtocol.cpp) and the weather.dat codec (src/sleepcards/WeatherCache.cpp).
# The HTTPS half (network/WeatherFetch) is device-only. No test touches the network.
add_executable(WeatherProtocolTest
  WeatherProtocolTest.cpp
  ${REPO_ROOT}/src/network/WeatherProtocol.cpp
  ${REPO_ROOT}/src/sleepcards/WeatherCache.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(WeatherProtocolTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
)

target_compile_definitions(WeatherProtocolTest PRIVATE WEATHER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

target_link_libraries(WeatherProtocolTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WeatherProtocolTest)
