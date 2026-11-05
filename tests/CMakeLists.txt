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
    BUILD_RPATH "${DAS_OBS_FRAMEWORK_DIR}")
  add_dependencies(das_kai_obs_harness das_kai_obs)
endif()
endif() # APPLE

# Not a ctest: attaches to the real ring while a real DAW plays (OBS closed).
add_executable(das_kai_live_monitor live_monitor.cpp)
target_link_libraries(das_kai_live_monitor PRIVATE das_sync das_transport)

# Not a ctest: replays a das_kai_live_monitor recording through the current code.
add_executable(das_kai_replay_trace replay_trace.cpp)
target_link_libraries(das_kai_replay_trace PRIVATE das_audio_core das_sync das_transport)

if(APPLE)
# DAS Kai Setup.app: install / update / uninstall / rollback in a throw-away
# home folder (built by the das_kai_setup target in the top-level CMakeLists).
add_test(NAME setup COMMAND "${DAS_SETUP_TESTS}")
# Promises that span files: Setup never asks for privileges or touches
# security attributes, and every file names the same bundles and repository.
add_test(NAME setup_contract
         COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_SOURCE_DIR}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/setup_contract.cmake")
endif() # APPLE
