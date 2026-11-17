find_package(Threads REQUIRED)

add_executable(das_audio_core_tests audio_core_tests.cpp)
target_link_libraries(das_audio_core_tests PRIVATE das_audio_core)
add_test(NAME audio_core COMMAND das_audio_core_tests)

add_executable(das_transport_tests transport_tests.cpp)
target_link_libraries(das_transport_tests PRIVATE das_transport Threads::Threads)
add_test(NAME transport COMMAND das_transport_tests)

if(WIN32)
  add_executable(das_transport_ipc_tests transport_ipc_tests_win.cpp)
else()
  add_executable(das_transport_ipc_tests transport_ipc_tests_posix.cpp)
endif()
target_link_libraries(das_transport_ipc_tests PRIVATE das_transport)
add_test(NAME transport_ipc COMMAND das_transport_ipc_tests)
set_tests_properties(transport_ipc PROPERTIES TIMEOUT 20)

add_executable(das_kai_sync_tests sync_tests.cpp)
target_link_libraries(das_kai_sync_tests PRIVATE das_audio_core das_sync das_transport)
add_test(NAME sync COMMAND das_kai_sync_tests)
set_tests_properties(sync PROPERTIES TIMEOUT 300)

juce_add_console_app(das_kai_vst3_smoke PRODUCT_NAME "DAS Kai VST3 Smoke Test")
target_sources(das_kai_vst3_smoke PRIVATE vst3_smoke.cpp)
target_compile_definitions(das_kai_vst3_smoke PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(das_kai_vst3_smoke PRIVATE
  das_sync
  das_transport
  juce::juce_audio_processors
  juce::juce_audio_utils
)
add_dependencies(das_kai_vst3_smoke DasKai_VST3)
if(WIN32)
  target_link_libraries(das_kai_vst3_smoke PRIVATE winmm)
endif()
add_test(NAME vst3_smoke COMMAND das_kai_vst3_smoke
  "$<TARGET_FILE_DIR:DasKai_VST3>/../..")
set_tests_properties(vst3_smoke PROPERTIES TIMEOUT 60)

# macOS-only developer tools below (CoreAudio, screenshots, libobs from
# OBS.app). Windows builds the tests above, the live monitor and the replayer.
if(APPLE)
# Not a ctest: runs the real VST3 from an audio device callback (see the file).
juce_add_console_app(das_kai_realtime_host PRODUCT_NAME "DAS Kai Realtime Host")
target_sources(das_kai_realtime_host PRIVATE realtime_host.cpp)
target_compile_definitions(das_kai_realtime_host PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(das_kai_realtime_host PRIVATE
  juce::juce_audio_processors
  juce::juce_audio_devices
)

juce_add_gui_app(das_kai_editor_host PRODUCT_NAME "DAS Kai UI Test")
target_sources(das_kai_editor_host PRIVATE vst3_editor_host.cpp)
target_compile_definitions(das_kai_editor_host PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(das_kai_editor_host PRIVATE
  juce::juce_audio_processors
  juce::juce_gui_extra
)
add_dependencies(das_kai_editor_host DasKai_VST3)

# Headless libobs check (manual, not part of ctest): needs /Applications/OBS.app.
#   tests/das_kai_obs_harness <das-kai-obs.plugin> [seconds] [ppm] [offset-at-s]
if(DAS_BUILD_OBS_PLUGIN)
  add_executable(das_kai_obs_harness obs_harness.cpp)
  target_include_directories(das_kai_obs_harness PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-source/Source")
  target_include_directories(das_kai_obs_harness SYSTEM PRIVATE
    "${obs_headers_SOURCE_DIR}/libobs" "${simde_SOURCE_DIR}")
  target_link_libraries(das_kai_obs_harness PRIVATE das_audio_core das_transport "${DAS_OBS_LIBRARY}")
  set_target_properties(das_kai_obs_harness PROPERTIES
    OSX_ARCHITECTURES arm64 # libobs in OBS.app is arm64 only
    BUILD_RPATH "${DAS_OBS_FRAMEWORK_DIR}")
  add_dependencies(das_kai_obs_harness das_kai_obs)
endif()
endif() # APPLE

# The README's download links follow the project version (see the script).
add_test(NAME readme_links
         COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_SOURCE_DIR}" "-DVERSION=${PROJECT_VERSION}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/readme_links.cmake")

# Not a ctest: attaches to the real ring while a real DAW plays (OBS closed).
add_executable(das_kai_live_monitor live_monitor.cpp)
target_link_libraries(das_kai_live_monitor PRIVATE das_sync das_transport)

# Not a ctest: replays a das_kai_live_monitor recording through the current code.
add_executable(das_kai_replay_trace replay_trace.cpp)
target_link_libraries(das_kai_replay_trace PRIVATE das_audio_core das_sync das_transport)

if(APPLE)
# The DAW plug-ins hold arm64 and x86_64, the OBS plug-in arm64 only (see the
# top-level CMakeLists). Fails on a build folder whose cache still says arm64.
add_test(NAME mac_archs
         COMMAND "${CMAKE_COMMAND}"
                 "-DVST3=$<TARGET_FILE:DasKai_VST3>" "-DAU=$<TARGET_FILE:DasKai_AU>"
                 "-DOBS=$<$<BOOL:${DAS_BUILD_OBS_PLUGIN}>:$<TARGET_FILE:das_kai_obs>>"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/mac_archs.cmake")

# A DAW opened with Rosetta runs the x86_64 slice while OBS stays arm64. These
# run that slice through Rosetta: the transfer across the two architectures
# (ring, clock model, controls), the real VST3 in an x86_64 host, and the
# sender and clock code. Skipped where Rosetta is not installed.
execute_process(COMMAND /usr/bin/arch -x86_64 /usr/bin/true
                RESULT_VARIABLE das_rosetta_result OUTPUT_QUIET ERROR_QUIET)
if(das_rosetta_result EQUAL 0 AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES
   AND "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES)
  add_test(NAME transport_ipc_rosetta COMMAND das_transport_ipc_tests)
  set_tests_properties(transport_ipc_rosetta PROPERTIES
    TIMEOUT 40 ENVIRONMENT "DAS_IPC_PRODUCER_ARCH=x86_64;DAS_IPC_CONSUMER_ARCH=arm64")
  add_test(NAME vst3_smoke_rosetta
           COMMAND /usr/bin/arch -x86_64 "$<TARGET_FILE:das_kai_vst3_smoke>"
                   "$<TARGET_FILE_DIR:DasKai_VST3>/../..")
  set_tests_properties(vst3_smoke_rosetta PROPERTIES TIMEOUT 120)
  add_test(NAME sync_rosetta
           COMMAND /usr/bin/arch -x86_64 "$<TARGET_FILE:das_kai_sync_tests>")
  set_tests_properties(sync_rosetta PROPERTIES TIMEOUT 900)
else()
  message(WARNING "Rosetta tests are not registered (Rosetta missing, or the "
                  "build is not arm64 + x86_64): ${CMAKE_OSX_ARCHITECTURES}")
endif()

# DAS Kai Setup.app: install / update / uninstall / rollback in a throw-away
# home folder (built by the das_kai_setup target in the top-level CMakeLists).
add_test(NAME setup COMMAND "${DAS_SETUP_TESTS}")
# Promises that span files: Setup never asks for privileges or touches
# security attributes, and every file names the same bundles and repository.
add_test(NAME setup_contract
         COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_SOURCE_DIR}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/setup_contract.cmake")
endif() # APPLE
