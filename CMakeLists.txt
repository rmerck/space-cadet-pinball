cmake_minimum_required(VERSION 3.26)

# iOS is a second Apple platform, chosen with -DCMAKE_SYSTEM_NAME=iOS. That
# is read here from the cache, where -D put it, because the deployment target
# below has to be settled before project().
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(PINBALL_IOS ON)
else()
  set(PINBALL_IOS OFF)
endif()

# The oldest macOS a self-contained build (PINBALL_FETCH_DEPS) runs on, and
# the LSMinimumSystemVersion of its application bundle. 11.0 is the first
# release for Apple silicon; SDL 3.4 itself asks for 10.13, and the C++ the
# game uses for nothing newer. It has to be set before project(), which reads
# it, so the option is read here from the cache, where -D put it. A value
# given on the command line or in the environment wins. A build against
# installed libraries is left to target the macOS it is built on, as those
# libraries do. Other systems ignore all of this. On iOS the variable means
# the oldest iOS instead.
if(PINBALL_IOS)
  if("${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum iOS version" FORCE)
  endif()
  # Every iPhone and iPad this runs on is arm64, and so is the simulator on
  # the Macs that can build it. Also read by project().
  if("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Architecture of the iOS build" FORCE)
  endif()
elseif(CMAKE_HOST_APPLE AND PINBALL_FETCH_DEPS AND "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL ""
   AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# The game's name, written here and nowhere else: renaming the game is a
# change to this one line. The identifiers below follow from it. The source
# reads them all from a generated header (src/core/Version.hpp includes it),
# and the bundle's Info.plist, the window title, the panel, the About screen
# and the high score directory are filled in from the same values, so they
# cannot drift apart. The version is the one in project().
set(PINBALL_APP_NAME "Multiball Academy")
# Words of letters and digits with one space between them, the first starting
# with a letter, and no more than the 30 characters the App Store allows a
# name. It becomes a file name, a bundle identifier and a directory name, so
# nothing else may be in it: no space at either end and no two in a row.
string(LENGTH "${PINBALL_APP_NAME}" pinball_name_length)
if(NOT PINBALL_APP_NAME MATCHES "^[A-Za-z][A-Za-z0-9]*( [A-Za-z0-9]+)*$"
   OR pinball_name_length LESS 2 OR pinball_name_length GREATER 30)
  message(FATAL_ERROR "PINBALL_APP_NAME must be 2 to 30 letters, digits and single spaces, "
                      "starting with a letter and not ending with a space: the identifiers "
                      "are made from it. It is '${PINBALL_APP_NAME}'")
endif()
# The short name under the icon on an iPhone's or iPad's home screen, which
# has room for about a dozen characters. Everywhere else the game goes by
# its full name.
set(PINBALL_HOME_NAME "Multiball")
string(LENGTH "${PINBALL_HOME_NAME}" pinball_home_name_length)
if(NOT PINBALL_HOME_NAME MATCHES "^[A-Za-z][A-Za-z0-9]*( [A-Za-z0-9]+)*$"
   OR pinball_home_name_length GREATER 30)
  message(FATAL_ERROR "PINBALL_HOME_NAME must be letters, digits and single spaces, at most 30. "
                      "It is '${PINBALL_HOME_NAME}'")
endif()
# The name in lower case, with hyphens for its spaces and with them removed:
# "multiball-academy" and "multiballacademy".
string(TOLOWER "${PINBALL_APP_NAME}" pinball_name_lower)
string(REPLACE " " "-" pinball_name_hyphens "${pinball_name_lower}")
string(REPLACE " " "" pinball_name_joined "${pinball_name_lower}")
# The application identifier: the bundle id, and what SDL is told.
set(PINBALL_BUNDLE_ID "io.github.rmerck.${pinball_name_joined}")
# The high scores live in <per-user data>/<PINBALL_DATA_ORG>/<PINBALL_DATA_DIR>/.
# An empty PINBALL_DATA_ORG leaves that level out, which is how it is: one
# directory, "multiball-academy". (Version 1.0.0, under its old name, kept
# its scores in a directory of its own. That one is never read or removed.)
set(PINBALL_DATA_ORG "")
set(PINBALL_DATA_DIR "${pinball_name_hyphens}")
# Where the privacy policy is published: docs/PRIVACY.md, as GitHub shows it.
set(PINBALL_PRIVACY_URL "https://github.com/rmerck/multiball-academy/blob/main/docs/PRIVACY.md")
# It goes into the source as a string and, on a touch screen, to the system's
# browser: a plain page of this repository on GitHub (cmake/ProductUrl.cmake).
include(cmake/ProductUrl.cmake)
pinball_check_url("${PINBALL_PRIVACY_URL}" pinball_privacy_url_ok)
if(NOT pinball_privacy_url_ok)
  message(FATAL_ERROR "PINBALL_PRIVACY_URL must be a page of https://github.com/rmerck/multiball-academy/ "
                      "with a plain path (no .., query or fragment). It is '${PINBALL_PRIVACY_URL}'")
endif()
# The public contact address for support, shown on the About page and in
# docs/SUPPORT.md and docs/PRIVACY.md. The display has capitals, digits and
# a few marks only, so it must be plain: lower case letters, digits, dots.
set(PINBALL_SUPPORT_EMAIL "dev@mrck.app")
if(NOT PINBALL_SUPPORT_EMAIL MATCHES "^[a-z0-9]+(\\.[a-z0-9]+)*@[a-z0-9]+(\\.[a-z0-9]+)+$")
  message(FATAL_ERROR "PINBALL_SUPPORT_EMAIL must be a plain address: '${PINBALL_SUPPORT_EMAIL}'")
endif()
# The copyright line of the application bundles.
set(PINBALL_COPYRIGHT "Copyright 2026 rmerck. MIT licence.")
project(MultiballAcademy VERSION 2.0.0 LANGUAGES CXX)
# The bundle version (CFBundleVersion) on iOS and macOS, also shown beside
# the version. A whole number with no leading zero.
set(PINBALL_BUILD_NUMBER "1" CACHE STRING "Build number: must rise with every upload to the App Store")
string(LENGTH "${PINBALL_BUILD_NUMBER}" pinball_build_number_length)
if(NOT PINBALL_BUILD_NUMBER MATCHES "^[1-9][0-9]*$" OR pinball_build_number_length GREATER 9)
  message(FATAL_ERROR "PINBALL_BUILD_NUMBER must be a whole number from 1 to 999999999, "
                      "digits only. It is '${PINBALL_BUILD_NUMBER}'")
endif()
# Given on the command line for a signed iOS build and kept in the build
# directory alone. Empty: nothing is signed (cmake/Ios.cmake).
set(PINBALL_IOS_TEAM_ID "" CACHE STRING "Apple team id for iOS device builds; never committed")
string(LENGTH "${PINBALL_IOS_TEAM_ID}" pinball_team_id_length)
if(NOT PINBALL_IOS_TEAM_ID STREQUAL ""
   AND (NOT PINBALL_IOS_TEAM_ID MATCHES "^[A-Z0-9]+$" OR NOT pinball_team_id_length EQUAL 10))
  message(FATAL_ERROR "PINBALL_IOS_TEAM_ID must be an Apple team id: ten capital letters and digits")
endif()

# PINBALL_IOS was settled above from what the command line said, because the
# deployment target had to be chosen before project(). A toolchain file or a
# preset can name the system later than that: the build would then be set up
# for the Mac and compiled for something else. The other Apple systems
# (tvOS, visionOS, watchOS) are not supported at all.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT PINBALL_IOS)
  message(FATAL_ERROR "The target system is iOS, but it was not named on the command line. "
                      "Configure with -DCMAKE_SYSTEM_NAME=iOS (docs/BUILDING.md)")
endif()
if(APPLE AND NOT PINBALL_IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "Unsupported Apple system '${CMAKE_SYSTEM_NAME}': macOS and iOS only")
endif()
if(PINBALL_IOS)
  # The application bundle is put together by Xcode's build phases: the
  # asset catalog, the processed Info.plist, the resources and the signature.
  if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "The iOS build needs the Xcode generator: add -G Xcode")
  endif()
  if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "The iOS build is arm64 only, for devices and for the simulator. "
                        "CMAKE_OSX_ARCHITECTURES is '${CMAKE_OSX_ARCHITECTURES}'")
  endif()
endif()

# APPLE is true for iOS as well. What is only for the Mac asks this instead.
if(APPLE AND NOT PINBALL_IOS)
  set(PINBALL_MACOS ON)
else()
  set(PINBALL_MACOS OFF)
endif()

if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

if(APPLE)
  # For the menu bar hook (src/platform/MacMenu.mm) on macOS and the system
  # calls (src/platform/ios/IosSystem.mm) on iOS. Enabled here, before any
  # dependency is added, so every directory of the build knows the language.
  enable_language(OBJCXX)
endif()

# C++23 for every target, set before any target exists: the game, the tests,
# the tools and the fuzzer. (The pinned dependencies are C and Objective-C,
# which this does not touch. With MSVC, CMake passes /std:c++latest until
# it knows a C++23 flag for that compiler.)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
  # The same for the Objective-C++ file. CMake knows no C++23 flag for
  # Objective-C++ with Apple clang (up to 4.1 at least) and would stop with
  # "requires the language dialect OBJCXX23", so name the flag here. It is
  # the one CMake uses for C++ with the same compiler.
  if(CMAKE_OBJCXX_COMPILER_ID MATCHES "Clang"
     AND NOT DEFINED CMAKE_OBJCXX23_STANDARD_COMPILE_OPTION)
    set(CMAKE_OBJCXX23_STANDARD_COMPILE_OPTION "-std=c++2b")
    set(CMAKE_OBJCXX23_EXTENSION_COMPILE_OPTION "-std=gnu++2b")
  endif()
  set(CMAKE_OBJCXX_STANDARD 23)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Makes executables position independent at link time too, where supported.
include(CheckPIESupported)
check_pie_supported(LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PINBALL_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(PINBALL_WERROR "Treat warnings as errors" OFF)
option(PINBALL_FETCH_DEPS "Build pinned SDL3 and Box2D from source and link them statically" OFF)
option(PINBALL_FUZZ "Build only the parser fuzzer (clang with libFuzzer)" OFF)
option(PINBALL_REQUIRE_ALL_TESTS
  "Stop the configuration if a test cannot be registered (no Python 3, or the tools left out)"
  OFF)

if(PINBALL_IOS)
  # Libraries installed on the Mac are built for the Mac.
  if(NOT PINBALL_FETCH_DEPS)
    message(FATAL_ERROR "The iOS build compiles SDL3 and Box2D itself: add -DPINBALL_FETCH_DEPS=ON")
  endif()
  # CTest cannot run an iOS program, so no test that runs one is registered
  # (see the tests below), and "all tests" cannot be promised.
  if(PINBALL_REQUIRE_ALL_TESTS)
    message(FATAL_ERROR "PINBALL_REQUIRE_ALL_TESTS does not apply to iOS: the tests are built "
                        "and then run on a simulator by hand or by CI (docs/BUILDING.md)")
  endif()
  # A Release build keeps its debug information, which Xcode moves into a
  # .dSYM beside the application before it strips the executable
  # (cmake/Ios.cmake). For every target, the dependencies included.
  # A language SDL enables later takes it from the _INIT variable, to which
  # CMake adds its own Release flags.
  foreach(language IN ITEMS C CXX OBJC OBJCXX)
    if(CMAKE_${language}_COMPILER_LOADED)
      string(APPEND CMAKE_${language}_FLAGS_RELEASE " -g")
    else()
      set(CMAKE_${language}_FLAGS_RELEASE_INIT "-g")
    endif()
  endforeach()
  # Only the application goes into an archive, not the static libraries.
  # And an archive build must not strip the libraries on the way: their
  # debug information has to reach the application's .dSYM. The application
  # itself is stripped (cmake/Ios.cmake).
  set(CMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL YES)
  set(CMAKE_XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT NO)
endif()

include(cmake/Hardening.cmake)

# The generated header that carries the name, the identifiers and the
# version from the block at the top to the source.
set(PINBALL_VERSION_DIR ${CMAKE_BINARY_DIR}/generated_version)
configure_file(cmake/VersionConfig.hpp.in ${PINBALL_VERSION_DIR}/VersionConfig.hpp @ONLY)

if(PINBALL_FUZZ)
  # The fuzzer exercises the core parsers alone: no SDL, no window.
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "PINBALL_FUZZ needs clang with libFuzzer (not Apple clang)")
  endif()
  if(NOT PINBALL_SANITIZE)
    message(WARNING "PINBALL_FUZZ without PINBALL_SANITIZE finds far less: add -DPINBALL_SANITIZE=ON")
  endif()
  add_library(pinball_fuzz_core STATIC
    src/core/Args.cpp
    src/core/AssetName.cpp
    src/core/HighScores.cpp
    src/core/OverlayLayout.cpp
    src/core/PngHeader.cpp
    src/core/ScreenLayout.cpp
    src/core/TouchControls.cpp)
  target_include_directories(pinball_fuzz_core PUBLIC src)
  pinball_apply_hardening(pinball_fuzz_core PUBLIC)
  target_compile_options(pinball_fuzz_core PUBLIC -fsanitize=fuzzer-no-link)
  add_executable(pinball_fuzz tests/fuzz/FuzzParsers.cpp)
  target_link_libraries(pinball_fuzz PRIVATE pinball_fuzz_core)
  target_link_options(pinball_fuzz PRIVATE -fsanitize=fuzzer)
  return()
endif()

if(PINBALL_FETCH_DEPS)
  include(cmake/PinnedDeps.cmake)
else()
  # (Not for iOS: refused above.)
  # 3.4 is the first release with the built-in PNG loader used for assets.
  find_package(SDL3 3.4 REQUIRED CONFIG)
  find_package(box2d 3.2 REQUIRED CONFIG)
endif()

# Platform-independent game logic. No SDL dependency so it is unit-testable headless.
add_library(pinball_core STATIC
  src/core/Args.cpp
  src/core/AssetManifest.cpp
  src/core/AssetName.cpp
  src/core/Format.cpp
  src/core/Game.cpp
  src/core/HighScores.cpp
  src/core/Courses.cpp
  src/core/LightShow.cpp
  src/core/OverlayLayout.cpp
  src/core/Physics.cpp
  src/core/PngHeader.cpp
  src/core/Rules.cpp
  src/core/Scenes.cpp
  src/core/ScreenLayout.cpp
  src/core/Sha256.cpp
  src/core/Music.cpp
  src/core/Sound.cpp
  src/core/TouchControls.cpp
)
target_include_directories(pinball_core PUBLIC src ${PINBALL_VERSION_DIR})
target_link_libraries(pinball_core PUBLIC box2d::box2d)
# PUBLIC so the same warnings, hardening and sanitizer flags reach every consumer.
pinball_apply_hardening(pinball_core PUBLIC)
# No fused multiply-add: a compiler may otherwise merge a multiply and an add
# on processors that have the instruction (every arm64, x86-64 with FMA),
# which rounds differently and sends the ball another way. With this the
# same input gives the same game on x86-64 and arm64, and the trajectory the
# tests record holds everywhere. Box2D is already built this way.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(pinball_core PUBLIC -ffp-contract=off)
endif()
if(PINBALL_IOS)
  # For the few places where the source must tell iOS from macOS.
  target_compile_definitions(pinball_core PUBLIC PINBALL_IOS=1)
endif()

# SDL-facing code shared by the game and the snapshot tool.
add_library(pinball_platform STATIC
  src/platform/About.cpp
  src/platform/App.cpp
  src/platform/AppKeyboard.cpp
  src/platform/AppLifecycle.cpp
  src/platform/AppTouch.cpp
  src/platform/Assets.cpp
  src/platform/AudioOutput.cpp
  src/platform/EmbeddedManifest.cpp
  src/platform/Hud.cpp
  src/platform/InitialsScreen.cpp
  src/platform/PauseMenu.cpp
  src/platform/SafeFile.cpp
  src/platform/ScoreStore.cpp
  src/platform/TableEffects.cpp
  src/platform/TableRenderer.cpp
  src/platform/Text.cpp
  src/platform/TouchOverlay.cpp
)
target_link_libraries(pinball_platform PUBLIC pinball_core SDL3::SDL3)
if(PINBALL_MACOS)
  # The menu bar's About item is routed to the game's own About screen.
  target_sources(pinball_platform PRIVATE src/platform/MacMenu.mm)
  set_source_files_properties(src/platform/MacMenu.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(pinball_platform PRIVATE "-framework Cocoa")
elseif(PINBALL_IOS)
  # What the game asks of iOS itself.
  target_sources(pinball_platform PRIVATE src/platform/ios/IosSystem.mm)
  set_source_files_properties(src/platform/ios/IosSystem.mm PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc")
  # Named here, not left to SDL's own list, which follows SDL's options.
  target_link_libraries(pinball_platform PRIVATE
    "-framework Foundation" "-framework UIKit" "-framework AVFoundation")
endif()

if(PINBALL_MACOS)
  # On macOS the game is an application bundle named after the game,
  # with the executable inside under the same name: the Dock, the menu bar
  # and Activity Monitor all take their label from one or the other. The
  # CMake target is still called pinball.
  set(PINBALL_ICON ${CMAKE_SOURCE_DIR}/packaging/macos/AppIcon.icns)
  add_executable(pinball MACOSX_BUNDLE src/main.cpp ${PINBALL_ICON})
  set_source_files_properties(${PINBALL_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  # LSMinimumSystemVersion: the deployment target, or with none set the
  # macOS this is built on, which is what the compiler then targets.
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(PINBALL_MIN_MACOS ${CMAKE_OSX_DEPLOYMENT_TARGET})
  else()
    execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE PINBALL_MIN_MACOS
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT PINBALL_MIN_MACOS MATCHES "^[0-9]+\\.[0-9]+")
      set(PINBALL_MIN_MACOS "11.0")
    endif()
  endif()
  set_target_properties(pinball PROPERTIES
    OUTPUT_NAME "${PINBALL_APP_NAME}"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "${PINBALL_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER ${PINBALL_BUNDLE_ID}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PINBALL_BUILD_NUMBER}
    MACOSX_BUNDLE_ICON_FILE AppIcon
    MACOSX_BUNDLE_COPYRIGHT "${PINBALL_COPYRIGHT}")
elseif(PINBALL_IOS)
  # On iOS the system owns the main loop, so the game has another entry
  # point. The bundle's properties, its Info.plist and its resources are
  # set by the included file.
  add_executable(pinball MACOSX_BUNDLE src/main_ios.cpp)
  include(cmake/Ios.cmake)
else()
  add_executable(pinball src/main.cpp)
endif()
target_link_libraries(pinball PRIVATE pinball_platform)
# A Release game carries no debug map, so it names no directory of the
# machine that built it.
if(PINBALL_MACOS)
  target_link_options(pinball PRIVATE $<$<CONFIG:Release>:-Wl,-S>)
elseif(PINBALL_IOS)
  # Xcode strips the executable, after it has saved the debug information
  # (cmake/Ios.cmake). A warning from the linker stops the build, as one from
  # the compiler does.
  if(PINBALL_WERROR)
    target_link_options(pinball PRIVATE -Wl,-fatal_warnings)
  endif()
elseif(NOT MSVC AND NOT APPLE)
  # And no symbol names either.
  target_link_options(pinball PRIVATE $<$<CONFIG:Release>:-Wl,-s>)
endif()

# Where the things that sit beside the game go: the development tools, the
# tests' scratch directories and the frames they render. Beside the bundle
# on macOS, never inside it, because anything added to a signed bundle
# breaks its signature. Elsewhere it is the directory of the executable, as
# before.
if(PINBALL_MACOS)
  set(PINBALL_BESIDE_DIR $<TARGET_BUNDLE_DIR:pinball>/..)
  set(PINBALL_GAME_ASSETS_DIR $<TARGET_BUNDLE_CONTENT_DIR:pinball>/Resources/assets)
elseif(PINBALL_IOS)
  # Neither is used: the assets are a resource of the application target
  # (cmake/Ios.cmake), and no tool or test is run from the build tree.
  set(PINBALL_BESIDE_DIR "")
  set(PINBALL_GAME_ASSETS_DIR "")
else()
  set(PINBALL_BESIDE_DIR $<TARGET_FILE_DIR:pinball>)
  set(PINBALL_GAME_ASSETS_DIR $<TARGET_FILE_DIR:pinball>/assets)
endif()

# Asset integrity: the size and SHA-256 of every asset are compiled into the
# executable, which checks the files against them before it starts. The
# manifest is regenerated on every build and rewritten only when it changes.
set(PINBALL_MANIFEST_DIR ${CMAKE_BINARY_DIR}/generated)
set(PINBALL_MANIFEST ${PINBALL_MANIFEST_DIR}/AssetManifest.inc)
file(MAKE_DIRECTORY ${PINBALL_MANIFEST_DIR})
add_custom_target(pinball_manifest
  COMMAND ${CMAKE_COMMAND} -DASSETS_DIR=${CMAKE_SOURCE_DIR}/assets -DOUTPUT=${PINBALL_MANIFEST}
          -P ${CMAKE_SOURCE_DIR}/cmake/GenerateAssetManifest.cmake
  BYPRODUCTS ${PINBALL_MANIFEST}
  COMMENT "Hashing assets"
  VERBATIM)
add_dependencies(pinball_platform pinball_manifest)
target_include_directories(pinball_platform PRIVATE ${PINBALL_MANIFEST_DIR})
set_source_files_properties(src/platform/EmbeddedManifest.cpp PROPERTIES
  OBJECT_DEPENDS ${PINBALL_MANIFEST})

# What this build is called, for the scripts and CI steps that check its
# results (tools/ios/check_bundle.sh, .github/workflows/ci.yml), so that
# none of them spells the name out again: lines of NAME='value'. They read
# it as text with tools/product_value.sh and never run it; all the same,
# nothing goes into it that could end a quoted value or a line.
if(NOT "${CMAKE_OSX_DEPLOYMENT_TARGET}" MATCHES "^[0-9.]*$")
  message(FATAL_ERROR "CMAKE_OSX_DEPLOYMENT_TARGET must be a version number. "
                      "It is '${CMAKE_OSX_DEPLOYMENT_TARGET}'")
endif()
foreach(value IN ITEMS "${PINBALL_APP_NAME}" "${PINBALL_HOME_NAME}" "${PINBALL_BUNDLE_ID}"
                       "${PROJECT_VERSION}" "${PINBALL_BUILD_NUMBER}"
                       "${CMAKE_OSX_DEPLOYMENT_TARGET}" "${PINBALL_MANIFEST}")
  if(value MATCHES "['\n\r]")
    message(FATAL_ERROR "A quote or a line break in '${value}': it cannot be written to "
                        "product.env. (Is there one in the build directory's path?)")
  endif()
endforeach()
# Whether this application ends itself after --frames (cmake/Ios.cmake), as
# ON or OFF and nothing else, so that scripts compare one word. Off iOS the
# option does not exist and the answer is OFF.
if(PINBALL_IOS_TEST_EXIT)
  set(pinball_test_exit_text ON)
else()
  set(pinball_test_exit_text OFF)
endif()
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/product.env @ONLY CONTENT
"# Written by CMake from the block at the top of CMakeLists.txt. Do not edit.
PINBALL_APP_NAME='@PINBALL_APP_NAME@'
PINBALL_HOME_NAME='@PINBALL_HOME_NAME@'
PINBALL_BUNDLE_ID='@PINBALL_BUNDLE_ID@'
PINBALL_VERSION='@PROJECT_VERSION@'
PINBALL_BUILD_NUMBER='@PINBALL_BUILD_NUMBER@'
PINBALL_IOS_TEST_EXIT='@pinball_test_exit_text@'
PINBALL_DEPLOYMENT_TARGET='@CMAKE_OSX_DEPLOYMENT_TARGET@'
PINBALL_ASSET_MANIFEST='@PINBALL_MANIFEST@'
")

# The game loads assets from an "assets" directory under SDL's base path:
# next to the executable, or Contents/Resources inside the macOS bundle.
# Copied by a target that runs on every build (not only on relink), so an
# edited asset is never stale. Nothing is ever deleted: a removed asset leaves
# an unused file behind in the build tree, which is harmless (on macOS the
# manifest check never opens it, but a signed bundle lists it until the next
# clean build).
# (Not on iOS, where Xcode copies them into the bundle: cmake/Ios.cmake.)
if(NOT PINBALL_IOS)
  add_custom_target(pinball_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_SOURCE_DIR}/assets ${PINBALL_GAME_ASSETS_DIR}
    COMMENT "Copying assets"
    VERBATIM)
  # The generator expressions add no dependency, so this is not circular. It
  # makes building just the pinball target bring its assets along.
  add_dependencies(pinball pinball_assets)
endif()

# macOS hardened runtime: the system then ignores DYLD_INSERT_LIBRARIES and
# refuses to load any library that is not Apple's. That needs the game to be
# self-contained, so it is tied to the statically linked pinned dependencies.
# Applied to Release builds. By default the signature is ad hoc: it turns the
# protections on but does not identify a publisher. PINBALL_CODESIGN_IDENTITY
# names a certificate to sign with instead (a Developer ID Application one
# for a build that is to be notarised: tools/notarize.sh). No entitlements
# are used: the game needs no network, no JIT and no exemption. (macOS only:
# an iOS bundle is signed by Xcode.)
#
# The whole bundle is signed, which seals the Info.plist, the icon and the
# assets with the executable. Signing therefore has to come last, and it is
# its own target that runs on every build, after the executable is linked and
# the assets are copied: a POST_BUILD step would only run on a relink and
# leave the seal stale after an asset changed. Anything that alters the
# bundle afterwards invalidates the signature until the next build.
if(PINBALL_MACOS)
  set(PINBALL_CODESIGN_IDENTITY "" CACHE STRING
    "Code signing identity for the macOS bundle (empty: ad hoc), e.g. Developer ID Application: NAME (TEAMID)")
  set(pinball_sign OFF)
  if(PINBALL_FETCH_DEPS AND NOT PINBALL_SANITIZE)
    option(PINBALL_HARDENED_RUNTIME "Sign the game with the hardened runtime" ON)
    set(pinball_sign ${PINBALL_HARDENED_RUNTIME})
  endif()
  if(PINBALL_CODESIGN_IDENTITY AND NOT pinball_sign)
    message(FATAL_ERROR "PINBALL_CODESIGN_IDENTITY signs with the hardened runtime, which needs "
                        "-DPINBALL_FETCH_DEPS=ON, no sanitizers and PINBALL_HARDENED_RUNTIME left ON")
  endif()
  if(pinball_sign)
    if(PINBALL_CODESIGN_IDENTITY)
      # A secure timestamp, which notarisation requires (needs the network).
      set(pinball_sign_args --sign ${PINBALL_CODESIGN_IDENTITY} --options runtime --timestamp)
    else()
      set(pinball_sign_args --sign - --options runtime)
    endif()
    # Release only: a debugger cannot attach to a hardened process, so the
    # configurations meant for debugging are left alone.
    add_custom_target(pinball_sign ALL
      COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Release>,env,true>
              codesign --force ${pinball_sign_args} $<TARGET_BUNDLE_DIR:pinball>
      COMMENT "Signing the bundle with the hardened runtime (Release only)"
      VERBATIM)
    add_dependencies(pinball_sign pinball)
  endif()
endif()

# Development tool that renders a frame offscreen to a PNG. Not part of the
# game; built with the tests.
option(PINBALL_BUILD_TOOLS "Build development tools" ON)
if(PINBALL_IOS)
  # They are programs for the machine that builds, and an iOS build makes
  # programs for iOS. Use a desktop build for them.
  set(PINBALL_BUILD_TOOLS OFF)
endif()
if(PINBALL_BUILD_TOOLS)
  add_executable(pinball_snapshot tools/snapshot/main.cpp)
  target_link_libraries(pinball_snapshot PRIVATE pinball_platform)
  # It sets up scenes that play does not reach through the tests' seam.
  target_include_directories(pinball_snapshot PRIVATE tests)
  # It sits beside the game and reads an assets directory next to itself,
  # through the same verifying loader. Off macOS that is the game's own
  # directory. On macOS the tools stay out of the bundle and get a copy of
  # their own, kept up to date the same way.
  set_target_properties(pinball_snapshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PINBALL_BESIDE_DIR})
  if(PINBALL_MACOS)
    add_custom_target(pinball_tool_assets
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              ${CMAKE_SOURCE_DIR}/assets ${PINBALL_BESIDE_DIR}/assets
      COMMENT "Copying assets for the tools"
      VERBATIM)
    add_dependencies(pinball_snapshot pinball_tool_assets)
  else()
    add_dependencies(pinball_snapshot pinball_assets)
  endif()

  # Plays scripted scenes of the light show and writes the playfield as a
  # run of PNGs (review GIFs, before and after, draw times).
  add_executable(pinball_fxreel tools/fxreel/main.cpp)
  target_link_libraries(pinball_fxreel PRIVATE pinball_platform)
  target_include_directories(pinball_fxreel PRIVATE tests)
  set_target_properties(pinball_fxreel PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PINBALL_BESIDE_DIR})
  if(PINBALL_MACOS)
    add_dependencies(pinball_fxreel pinball_tool_assets)
  else()
    add_dependencies(pinball_fxreel pinball_assets)
  endif()

  # Prints the table geometry as JSON for the art scripts.
  add_executable(pinball_layout tools/layout_dump/main.cpp)
  target_link_libraries(pinball_layout PRIVATE pinball_core)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(core_tests
    tests/main.cpp
    tests/AcceptanceTest.cpp
    tests/ArgsTest.cpp
    tests/AssetNameTest.cpp
    tests/BumperTest.cpp
    tests/ElementTest.cpp
    tests/FixedTimestepTest.cpp
    tests/FlipperTest.cpp
    tests/FormatTest.cpp
    tests/GameTest.cpp
    tests/HighScoreTest.cpp
    tests/HoleTest.cpp
    tests/LightShowTest.cpp
    tests/LaunchTest.cpp
    tests/CourseTest.cpp
    tests/MultiballTest.cpp
    tests/NudgeTest.cpp
    tests/PhysicsTest.cpp
    tests/PlungerTest.cpp
    tests/PngHeaderTest.cpp
    tests/RuleFuzzTest.cpp
    tests/RulesTest.cpp
    tests/ScreenLayoutTest.cpp
    tests/Sha256Test.cpp
    tests/MusicTest.cpp
    tests/SoundTest.cpp
    tests/TableGeometryTest.cpp
    tests/TargetTest.cpp
    tests/TouchControlsTest.cpp
    tests/bots/Acceptance.cpp
    tests/bots/TableHarness.cpp
  )
  target_include_directories(core_tests PRIVATE tests)
  target_link_libraries(core_tests PRIVATE pinball_core)
  # (No test is registered for iOS: see below, where an iOS build stops.)
  if(NOT PINBALL_IOS)
    add_test(NAME core_tests COMMAND core_tests)
  endif()

  # Scratch and output files go beside the binaries under test (beside the
  # bundle on macOS, not in it), so with a multi-config generator each
  # configuration has its own.
  set(PINBALL_TEST_DIR ${PINBALL_BESIDE_DIR})
  # Whatever a test starts gets a per-user preferences directory inside the
  # build tree, so a test run cannot touch the player's real high score file.
  # XDG_DATA_HOME is how SDL is told on Linux; macOS has no such switch.
  set(PINBALL_HEADLESS_ENV
    "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test_prefs")

  # File handling: bounded reads, refusal of links and pipes, atomic saves.
  add_executable(safefile_tests tests/SafeFileTest.cpp)
  target_link_libraries(safefile_tests PRIVATE pinball_platform)
  if(NOT PINBALL_IOS)
    add_test(NAME safe_files
      COMMAND ${CMAKE_COMMAND} -DTEST=$<TARGET_FILE:safefile_tests>
              -DSCRATCH=$<TARGET_FILE_DIR:safefile_tests>/safefile_scratch
              -P ${CMAKE_SOURCE_DIR}/tests/RunInScratch.cmake)
  endif()

  # The same tests with SafeFile.cpp built without the single-call
  # move-without-replace (renameat2, renamex_np), so that the link() and
  # unlink() path other systems and file systems fall back to is compiled
  # and run on every system. SafeFile.cpp is compiled here a second time:
  # the copy in pinball_platform is the normal one. On a system that is not
  # POSIX the macro changes nothing and this repeats safe_files.
  add_executable(safefile_fallback_tests tests/SafeFileTest.cpp src/platform/SafeFile.cpp)
  target_compile_definitions(safefile_fallback_tests PRIVATE PINBALL_NO_ATOMIC_NOREPLACE)
  # The include path, warnings and hardening come with pinball_core; SDL is
  # for SafeFile's portable half. Not pinball_platform, which has the normal
  # SafeFile in it.
  target_link_libraries(safefile_fallback_tests PRIVATE pinball_core SDL3::SDL3)
  if(NOT PINBALL_IOS)
    add_test(NAME safe_files_fallback
      COMMAND ${CMAKE_COMMAND} -DTEST=$<TARGET_FILE:safefile_fallback_tests>
              -DSCRATCH=$<TARGET_FILE_DIR:safefile_fallback_tests>/safefile_fallback_scratch
              -P ${CMAKE_SOURCE_DIR}/tests/RunInScratch.cmake)
  endif()

  if(PINBALL_IOS)
    # An iOS build stops here. CTest runs its tests on the machine that
    # builds, and that machine cannot run an iOS program, so `ctest` in an
    # iOS build directory has nothing it can pass; the tests that start the
    # game, the tools and the render tests belong to a desktop build of the
    # same source. The three programs above are built, as small unsigned
    # bundles, and run on a simulator with `xcrun simctl spawn`
    # (docs/BUILDING.md; CI does). That covers the core and the file
    # handling with the iOS SDK and its standard library.
    foreach(test IN ITEMS core_tests safefile_tests safefile_fallback_tests)
      string(REPLACE "_" "-" test_id "${test}")
      set_target_properties(${test} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER ${PINBALL_BUNDLE_ID}.${test_id}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${PINBALL_BUNDLE_ID}.${test_id}
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
    endforeach()
    return()
  endif()

  # The application's event handling (keys, touches, window changes, the
  # mobile life cycle): the real App on SDL's dummy drivers, with no score
  # file. It reads the assets beside itself, as the tools do, so it is built
  # with them and after them. About a minute under the sanitizers, most of
  # it waiting on the software renderer's paced presents: labelled slow, so
  # that `ctest -LE slow` is the quick run.
  if(PINBALL_BUILD_TOOLS AND NOT PINBALL_IOS)
    add_executable(app_tests tests/AppEventTest.cpp)
    target_include_directories(app_tests PRIVATE tests)
    target_link_libraries(app_tests PRIVATE pinball_platform)
    set_target_properties(app_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${PINBALL_BESIDE_DIR})
    add_dependencies(app_tests pinball_snapshot)
    add_test(NAME app_events COMMAND app_tests)
    set_tests_properties(app_events PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 300 LABELS slow)
  endif()

  # Python runs isolated (-I): no user site directory, no PYTHON* variables
  # and no import from the script's or the current directory.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    # Assets must be well-formed PNGs with no metadata.
    add_test(NAME assets_clean
      COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/check_assets.py
              ${CMAKE_SOURCE_DIR}/assets)

    # The renderer's copy of the art's palette (src/generated/ArtPalette.hpp)
    # and the iOS launch colour are what tools/art/palette.py says.
    add_test(NAME art_palette
      COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/art/gen_header.py --check
              ${CMAKE_SOURCE_DIR}/src/generated/ArtPalette.hpp
              ${CMAKE_SOURCE_DIR}/packaging/ios/Assets.xcassets/LaunchBackground.colorset/Contents.json)

    # The committed art is what tools/art/SHA256SUMS records, file for file.
    add_test(NAME art_sums
      COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/art/check_sums.py ${CMAKE_SOURCE_DIR})

    # The built game must refuse to start if any asset is altered.
    add_test(NAME asset_integrity
      COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/test_asset_integrity.py
              $<TARGET_FILE:pinball> ${PINBALL_TEST_DIR}/asset_integrity_scratch)
    set_tests_properties(asset_integrity PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 120)

    # The check a signed iOS build gets before upload refuses every
    # signature, entitlement and provisioning profile it should, tried
    # against stand-ins for Apple's tools. (Needs bash, so not on Windows.)
    if(UNIX)
      add_test(NAME check_signed_stubs
        COMMAND bash ${CMAKE_SOURCE_DIR}/tests/CheckSignedStubs.sh ${CMAKE_SOURCE_DIR})
      set_tests_properties(check_signed_stubs PROPERTIES TIMEOUT 120)
    endif()
  elseif(PINBALL_REQUIRE_ALL_TESTS)
    message(FATAL_ERROR "Python 3 not found, and PINBALL_REQUIRE_ALL_TESTS is ON: the "
                        "assets_clean, asset_integrity and layout_json tests need it")
  else()
    # Say so: a run without these would otherwise look complete.
    message(WARNING "Python 3 not found: the assets_clean, asset_integrity and "
                    "layout_json tests are not registered")
  endif()
  if(PINBALL_REQUIRE_ALL_TESTS AND NOT PINBALL_BUILD_TOOLS)
    message(FATAL_ERROR "PINBALL_BUILD_TOOLS is OFF, and PINBALL_REQUIRE_ALL_TESTS is ON: the "
                        "render and layout tests need the tools")
  endif()

  # Rendering smoke test: play ten seconds headless and draw a frame. The
  # tool fails if the frame is blank.
  if(PINBALL_BUILD_TOOLS)
    add_test(NAME render_snapshot
      COMMAND pinball_snapshot 1200 render_snapshot.png
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    add_test(NAME render_about
      COMMAND pinball_snapshot 60 render_about.png about
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    set_tests_properties(render_about PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    add_test(NAME render_initials
      COMMAND pinball_snapshot 60 render_initials.png initials
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    set_tests_properties(render_initials PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    set_tests_properties(render_snapshot PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    # The table's own moving parts: a ball locked in the Hangar, the bay
    # doors down, and the spinner turning.
    add_test(NAME render_locked
      COMMAND pinball_snapshot 60 render_locked.png locked
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    add_test(NAME render_bank_down
      COMMAND pinball_snapshot 60 render_bank_down.png bankdown
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    add_test(NAME render_spinning
      COMMAND pinball_snapshot 60 render_spinning.png spinning
      WORKING_DIRECTORY ${PINBALL_TEST_DIR})
    set_tests_properties(render_locked render_bank_down render_spinning PROPERTIES
      ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    # The tool must replace a link at its output name, never write through
    # it, even one that points nowhere. (Not on Windows, where making a link
    # needs a privilege.)
    if(UNIX)
      add_test(NAME snapshot_replaces_link
        COMMAND ${CMAKE_COMMAND} -DSNAPSHOT=$<TARGET_FILE:pinball_snapshot>
                -DSCRATCH=${PINBALL_TEST_DIR}/snapshot_link_scratch
                -P ${CMAKE_SOURCE_DIR}/tests/SnapshotReplacesLink.cmake)
      set_tests_properties(snapshot_replaces_link PROPERTIES
        ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60)
    endif()
  endif()

  # Renders of every screen layout. (None yet.)
  include(cmake/LayoutTests.cmake)
  # The table's acceptance tests: the statistical suite and its CTest label.
  include(cmake/AcceptanceTests.cmake)

  # A placeholder iOS icon cannot reach a shippable build, and the one
  # committed is not one.
  add_test(NAME ios_icon_guard
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DSCRATCH=${PINBALL_TEST_DIR}/ios_icon_guard_scratch
            -P ${CMAKE_SOURCE_DIR}/tests/CheckIosIconGuard.cmake)

  # The recorded SDL configure results an iOS build may start from hold
  # nothing but yes-or-no probe results (cmake/IosPreseed.cmake).
  add_test(NAME preseed_records
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/CheckPreseed.cmake)

  # The rule the privacy policy's address is held to at configure time.
  add_test(NAME product_url_rule
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/CheckProductUrl.cmake)

  # The geometry dump must be valid JSON.
  if(PINBALL_BUILD_TOOLS AND Python3_Interpreter_FOUND)
    add_test(NAME layout_json
      COMMAND Python3::Interpreter -I -c
              "import json,subprocess,sys; d=json.loads(subprocess.check_output([sys.argv[1]])); assert len(d['shell'])>10 and d['bumpers'] and d['holes']"
              $<TARGET_FILE:pinball_layout>)
  endif()

  # Headless smoke test: run the real binary with no display. The binary itself
  # fails if no simulation step ran.
  add_test(NAME smoke_headless COMMAND pinball --frames 120)
  set_tests_properties(smoke_headless PROPERTIES
    ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 30)

  # The macOS bundle: a valid Info.plist that carries the identifier and
  # version the game itself reports, the icon, the assets, and no stray
  # executable.
  if(PINBALL_MACOS)
    add_test(NAME bundle_layout
      COMMAND ${CMAKE_COMMAND} -DBUNDLE=$<TARGET_BUNDLE_DIR:pinball>
              -DEXECUTABLE=$<TARGET_FILE:pinball> "-DNAME=${PINBALL_APP_NAME}"
              -DIDENTIFIER=${PINBALL_BUNDLE_ID} -DVERSION=${PROJECT_VERSION}
              -DBUILD_NUMBER=${PINBALL_BUILD_NUMBER}
              "-DCOPYRIGHT=${PINBALL_COPYRIGHT}"
              -P ${CMAKE_SOURCE_DIR}/tests/CheckBundle.cmake)
    set_tests_properties(bundle_layout PROPERTIES TIMEOUT 30)
  endif()

  # Bad arguments must be rejected cleanly: exit code 1, usage text, and no
  # sanitizer report. Checked by a script because CTest's WILL_FAIL and
  # PASS_REGULAR_EXPRESSION cannot assert all three together.
  set(bad_args_cases
    "negative:--frames|-5"
    "zero:--frames|0"
    "too_large:--frames|1000001"
    "trailing_junk:--frames|5x"
    "missing_value:--frames"
    "unknown_flag:--bogus")
  # --scene is read by debug builds only: anywhere else it is an unknown
  # option, even with a good name (the --frames after it would end a run
  # that wrongly took it). A debug build refuses a name that is no scene.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND bad_args_cases "unknown_scene:--scene|bogus" "scene_missing_value:--scene")
  elseif(NOT CMAKE_CONFIGURATION_TYPES)
    list(APPEND bad_args_cases "scene_not_debug:--scene|table|--frames|5")
  endif()
  foreach(case IN LISTS bad_args_cases)
    string(REGEX MATCH "^[^:]+" name "${case}")
    string(REGEX REPLACE "^[^:]+:" "" args "${case}")
    add_test(NAME rejects_${name}
      COMMAND ${CMAKE_COMMAND} -DPINBALL=$<TARGET_FILE:pinball> -DARGS=${args}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ExpectUsageError.cmake)
    set_tests_properties(rejects_${name} PROPERTIES TIMEOUT 30)
  endforeach()
endif()
