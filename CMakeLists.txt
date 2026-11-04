cmake_minimum_required(VERSION 3.25)

# Must be set before project(): otherwise the build inherits this Mac's macOS
# version as the minimum and the bundles refuse to load on older systems.
# 13.0 matches upstream DawAudioStreamer's macOS preview. Apple Silicon only:
# Intel cannot be tested here.
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures")

# C is required: JUCE compiles a few C sources (SheenBidi) and the Makefile
# generator does not enable it implicitly the way Xcode does.
project(DasKai VERSION 0.1.0 LANGUAGES C CXX)

if(NOT APPLE)
  message(FATAL_ERROR "DAS Kai is a macOS-only fork")
endif()

set(DAS_COMPONENT_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Numeric x.y.z version embedded in built components")
set(DAS_PUBLISHER "Keinoaza" CACHE STRING "Vendor shown in hosts")

option(DAS_BUILD_TESTS "Build unit, simulation and smoke tests" ON)
option(DAS_BUILD_OBS_PLUGIN "Build the OBS audio source plug-in" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wpedantic)

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

if(DAS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
