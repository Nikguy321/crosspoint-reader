cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(html_to_plain_text)
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(book_sync)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(korean_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(home_button)
add_subdirectory(button_press_navigation)
add_subdirectory(bench_console)
add_subdirectory(auto_power_off)
add_subdirectory(sleep_ledger)
add_subdirectory(power_policy)
add_subdirectory(frontlight_pwm)
add_subdirectory(power_ledger)
add_subdirectory(live_sleep)
add_subdirectory(wifi_join_order)
# Nick's radio/clock rule is a source check, not a unit: run it with the suite.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME check_radio_power COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/check_radio_power.py)
endif()
add_subdirectory(almanac)
add_subdirectory(sleep_card_preview)
add_subdirectory(sleep_card_framework)
add_subdirectory(sleep_card_now_reading)
add_subdirectory(sleep_card_day)
add_subdirectory(sleep_card_calendar)
add_subdirectory(sleep_card_quote)
add_subdirectory(sleep_card_owner)
add_subdirectory(sleep_card_sky)
add_subdirectory(sleep_card_shuffle)
add_subdirectory(sleep_card_location)
add_subdirectory(geolocate)

add_subdirectory(inflate_stream)
add_subdirectory(fs_helpers)

add_subdirectory(absolute_grayscale)

add_subdirectory(sd_card_font)
add_subdirectory(glyph_bitmap)
