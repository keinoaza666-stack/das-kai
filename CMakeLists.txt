cmake_minimum_required(VERSION 3.25)

# Must be set before project(): otherwise the build inherits this Mac's macOS
# version as the minimum and the bundles refuse to load on older systems.
# 13.0 matches upstream DawAudioStreamer's macOS preview. Apple Silicon only:
# Intel cannot be tested here.
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures")

# C is required: JUCE compiles a few C sources (SheenBidi) and the Makefile
# generator does not enable it implicitly the way Xcode does.
project(DasKai VERSION 0.2.0 LANGUAGES C CXX)

if(NOT APPLE AND NOT WIN32)
  message(FATAL_ERROR "DAS Kai builds on macOS and Windows only")
endif()

if(MSVC)
  # Windows (2026-11-05, as upstream): the C++ runtime is linked into each
  # binary, so the plug-ins need no Visual C++ Redistributable on the user's PC.
  # Set before any target, JUCE's included.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Always the project version. It used to be a cache entry, which an existing
# build folder kept at the old value after project(VERSION) changed, so the
# plug-ins would still say 0.1.0 (found when bumping to 0.2.0).
unset(DAS_COMPONENT_VERSION CACHE)
set(DAS_COMPONENT_VERSION "${PROJECT_VERSION}")
set(DAS_PUBLISHER "Keinoaza" CACHE STRING "Vendor shown in hosts")

option(DAS_BUILD_TESTS "Build unit, simulation and smoke tests" ON)
option(DAS_BUILD_OBS_PLUGIN "Build the OBS audio source plug-in" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  # /utf-8: the sources hold Japanese text in UTF-8 (upstream's flags).
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Third-party sources are pinned to exact commits (checked 2026-11-04, see
# verify-map.md). Prefer copies in third-party/ for offline rebuilds.
set(DAS_BUNDLED_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party")
if(EXISTS "${DAS_BUNDLED_THIRD_PARTY_DIR}/juce-src/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_JUCE "${DAS_BUNDLED_THIRD_PARTY_DIR}/juce-src"
      CACHE PATH "Bundled JUCE source" FORCE)
endif()
if(EXISTS "${DAS_BUNDLED_THIRD_PARTY_DIR}/r8brain-src/CDSPResampler.h")
  set(FETCHCONTENT_SOURCE_DIR_R8BRAIN "${DAS_BUNDLED_THIRD_PARTY_DIR}/r8brain-src"
      CACHE PATH "Bundled r8brain source" FORCE)
endif()
if(EXISTS "${DAS_BUNDLED_THIRD_PARTY_DIR}/obs_headers-src/libobs/obs.h")
  set(FETCHCONTENT_SOURCE_DIR_OBS_HEADERS "${DAS_BUNDLED_THIRD_PARTY_DIR}/obs_headers-src"
      CACHE PATH "Bundled OBS source" FORCE)
endif()
if(EXISTS "${DAS_BUNDLED_THIRD_PARTY_DIR}/simde-src/simde/x86/sse2.h")
  set(FETCHCONTENT_SOURCE_DIR_SIMDE "${DAS_BUNDLED_THIRD_PARTY_DIR}/simde-src"
      CACHE PATH "Bundled SIMDe source" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 3af3ce009f6a02f6fa651008fffb5b41743a9fab # JUCE 8.0.10
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(
  r8brain
  GIT_REPOSITORY https://github.com/avaneev/r8brain-free-src.git
  GIT_TAG 8fff6f3db26f14a8f5e8fb871000613673db5753
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(r8brain)

if(DAS_BUILD_OBS_PLUGIN)
  FetchContent_Declare(
    obs_headers
    GIT_REPOSITORY https://github.com/obsproject/obs-studio.git
    GIT_TAG c17423ce05899ecb93f678601b3feaa8a469b180 # OBS 32.1.2
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR headers-only-no-cmake-project
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(obs_headers)

  FetchContent_Declare(
    simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde.git
    GIT_TAG 71fd833d9666141edcd1d3c109a80e228303d8d7
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR headers-only-no-cmake-project
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(simde)
endif()

add_subdirectory(libs/audio-core)
add_subdirectory(libs/transport)
add_subdirectory(libs/sync)
add_subdirectory(plugins/send-vst3)

if(DAS_BUILD_OBS_PLUGIN)
  add_subdirectory(plugins/obs-source)
endif()

if(APPLE)
# DAS Kai Setup.app (installer/macos) is Swift. The Makefile generator cannot
# build Swift, so swiftc is called directly. The app is compiled here too so a
# broken Setup fails the normal build, not only the packaging step.
set(DAS_SETUP_DIR "${CMAKE_BINARY_DIR}/setup")
set(DAS_SETUP_TESTS "${DAS_SETUP_DIR}/das_kai_setup_tests")
set(DAS_SETUP_SOURCES
    "${CMAKE_SOURCE_DIR}/installer/macos/SetupCore.swift"
    "${CMAKE_SOURCE_DIR}/installer/macos/SetupApp.swift")
add_custom_command(
  OUTPUT "${DAS_SETUP_TESTS}" "${DAS_SETUP_DIR}/DASKaiSetup"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DAS_SETUP_DIR}"
  COMMAND /usr/bin/xcrun swiftc -swift-version 5 -target arm64-apple-macosx13.0
          "${CMAKE_SOURCE_DIR}/installer/macos/SetupCore.swift"
          "${CMAKE_SOURCE_DIR}/tests/setup_tests.swift"
          -o "${DAS_SETUP_TESTS}"
  COMMAND /usr/bin/xcrun swiftc -swift-version 5 -no-stdlib-rpath
          -target arm64-apple-macosx13.0
          -framework AppKit ${DAS_SETUP_SOURCES} -o "${DAS_SETUP_DIR}/DASKaiSetup"
  DEPENDS ${DAS_SETUP_SOURCES} "${CMAKE_SOURCE_DIR}/tests/setup_tests.swift"
  COMMENT "Building DAS Kai Setup and its tests (Swift)"
  VERBATIM)
add_custom_target(das_kai_setup ALL DEPENDS "${DAS_SETUP_TESTS}")

# Distribution ZIP for GitHub releases: cmake --build <dir> --target das_kai_package
# Output: <build>/dist/DAS-Kai-<version>-macOS-AppleSilicon.zip (+ .sha256)
if(DAS_BUILD_OBS_PLUGIN)
  add_custom_target(das_kai_package
    COMMAND "${CMAKE_COMMAND}"
      "-DDAS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
      "-DDAS_BINARY_DIR=${CMAKE_BINARY_DIR}"
      "-DDAS_OUTPUT_DIR=${CMAKE_BINARY_DIR}/dist"
      "-DDAS_VERSION=${PROJECT_VERSION}"
      "-DDAS_VST3=$<TARGET_BUNDLE_DIR:DasKai_VST3>"
      "-DDAS_AU=$<TARGET_BUNDLE_DIR:DasKai_AU>"
      "-DDAS_OBS=$<TARGET_BUNDLE_DIR:das_kai_obs>"
      "-DDAS_SETUP_TESTS=${DAS_SETUP_TESTS}"
      "-DDAS_JUCE_DIR=${juce_SOURCE_DIR}"
      "-DDAS_OBS_HEADERS_DIR=${obs_headers_SOURCE_DIR}"
      "-DDAS_SIMDE_DIR=${simde_SOURCE_DIR}"
      -P "${CMAKE_SOURCE_DIR}/cmake/CreateMacPackage.cmake"
    DEPENDS DasKai_VST3 DasKai_AU das_kai_obs das_kai_setup
    COMMENT "Creating the DAS Kai distribution ZIP"
    VERBATIM)
endif()
endif() # APPLE

# Windows installer (installer/windows/DASKai.iss, Inno Setup 6):
#   cmake --build <dir> --config Release --target das_kai_package
# Output: <build>/dist/DAS-Kai-<version>-Windows-x64-Setup.exe
if(WIN32 AND DAS_BUILD_OBS_PLUGIN)
  find_program(DAS_ISCC_EXECUTABLE ISCC.exe
    HINTS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
          "C:/Program Files (x86)/Inno Setup 6")
  if(DAS_ISCC_EXECUTABLE)
    add_custom_target(das_kai_package
      COMMAND "${CMAKE_COMMAND}"
        "-DDAS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DDAS_OUTPUT_DIR=${CMAKE_BINARY_DIR}/dist"
        "-DDAS_VERSION=${PROJECT_VERSION}"
        "-DDAS_ISCC=${DAS_ISCC_EXECUTABLE}"
        # Not a bundle target on Windows: the DLL sits in
        # DAS Kai.vst3/Contents/x86_64-win, two folders below the bundle.
        "-DDAS_VST3=$<TARGET_FILE_DIR:DasKai_VST3>/../.."
        "-DDAS_OBS_DLL=$<TARGET_FILE:das_kai_obs>"
        "-DDAS_JUCE_DIR=${juce_SOURCE_DIR}"
        "-DDAS_OBS_HEADERS_DIR=${obs_headers_SOURCE_DIR}"
        "-DDAS_SIMDE_DIR=${simde_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/CreateWindowsInstaller.cmake"
      DEPENDS DasKai_VST3 das_kai_obs
      COMMENT "Creating the DAS Kai Windows installer"
      VERBATIM)
  endif()
endif()

if(DAS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
