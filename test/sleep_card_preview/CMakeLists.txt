# Sleep-screen cards on the host: the real GfxRenderer, the built-in fonts and every
# src/sleepcards file (except Device*.cpp, which needs the SD card, the RTC and the
# settings store) drawing into an in-memory framebuffer. SleepCardHost is the library the
# preview test and each card's own test (test/sleep_card_<name>/) link.
#
# A card agent may add files to src/sleepcards freely: they are globbed here. Everything in
# that folder must build on the host, apart from files named Device*.cpp.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# The i18n tables are generated (gitignored) files; the firmware build makes them in
# lib/I18n, and so does this, whenever a translation changes.
file(GLOB I18N_YAML CONFIGURE_DEPENDS ${REPO_ROOT}/lib/I18n/translations/*.yaml)
set(I18N_GENERATED
  ${REPO_ROOT}/lib/I18n/I18nKeys.h
  ${REPO_ROOT}/lib/I18n/I18nStrings.h
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
add_custom_command(
  OUTPUT ${I18N_GENERATED}
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/gen_i18n.py ${REPO_ROOT}/lib/I18n/translations ${REPO_ROOT}/lib/I18n/
  DEPENDS ${I18N_YAML} ${REPO_ROOT}/scripts/gen_i18n.py
  WORKING_DIRECTORY ${REPO_ROOT}
  COMMENT "Generating i18n tables"
)

file(GLOB SLEEP_CARD_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/src/sleepcards/*.cpp)
list(FILTER SLEEP_CARD_SOURCES EXCLUDE REGEX "/Device[^/]*\\.cpp$")
file(GLOB ALMANAC_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/lib/Almanac/*.cpp)

add_library(SleepCardHost STATIC
  CardPreview.cpp
  host/HalDisplay.cpp
  host/FontAlloc.cpp
  host/UzlibChecksums.c
  ${SLEEP_CARD_SOURCES}
  ${ALMANAC_SOURCES}
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${I18N_GENERATED}
)

# host/ first: its stand-ins (HalDisplay, BoardConfig, Arduino, Logging, HalStorage, ...)
# shadow the device headers.
target_include_directories(SleepCardHost PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/host
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Almanac
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
)
target_compile_definitions(SleepCardHost PUBLIC CARD_PREVIEW_REPO_ROOT="${REPO_ROOT}")
target_link_libraries(SleepCardHost PUBLIC crosspoint_test_common)
# The renderer and fonts are third-party-ish code built for the device's warnings, not -pedantic.
target_compile_options(SleepCardHost PRIVATE -Wno-pedantic -Wno-unused-parameter -Wno-missing-field-initializers
  -Wno-sign-compare -Wno-unused-variable -Wno-unused-function)

add_executable(SleepCardPreviewTest SleepCardPreviewTest.cpp)
target_link_libraries(SleepCardPreviewTest PRIVATE SleepCardHost GTest::gtest_main)
gtest_discover_tests(SleepCardPreviewTest)
