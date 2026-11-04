find_package(Threads REQUIRED)

add_executable(das_audio_core_tests audio_core_tests.cpp)
target_link_libraries(das_audio_core_tests PRIVATE das_audio_core)
add_test(NAME audio_core COMMAND das_audio_core_tests)

add_executable(das_transport_tests transport_tests.cpp)
target_link_libraries(das_transport_tests PRIVATE das_transport Threads::Threads)
add_test(NAME transport COMMAND das_transport_tests)

add_executable(das_transport_ipc_tests transport_ipc_tests_posix.cpp)
target_link_libraries(das_transport_ipc_tests PRIVATE das_transport)
add_test(NAME transport_ipc COMMAND das_transport_ipc_tests)
set_tests_properties(transport_ipc PROPERTIES TIMEOUT 20)

add_executable(kinu_stream_sync_tests sync_tests.cpp)
target_link_libraries(kinu_stream_sync_tests PRIVATE das_audio_core das_sync das_transport)
add_test(NAME sync COMMAND kinu_stream_sync_tests)
set_tests_properties(sync PROPERTIES TIMEOUT 300)

juce_add_console_app(kinu_stream_vst3_smoke PRODUCT_NAME "Kinu Stream VST3 Smoke Test")
target_sources(kinu_stream_vst3_smoke PRIVATE vst3_smoke.cpp)
target_compile_definitions(kinu_stream_vst3_smoke PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(kinu_stream_vst3_smoke PRIVATE
  das_sync
  das_transport
  juce::juce_audio_processors
  juce::juce_audio_utils
)
add_dependencies(kinu_stream_vst3_smoke KinuStream_VST3)
add_test(NAME vst3_smoke COMMAND kinu_stream_vst3_smoke
  "$<TARGET_FILE_DIR:KinuStream_VST3>/../..")
set_tests_properties(vst3_smoke PROPERTIES TIMEOUT 60)

juce_add_gui_app(kinu_stream_editor_host PRODUCT_NAME "Kinu Stream UI Test")
target_sources(kinu_stream_editor_host PRIVATE vst3_editor_host.cpp)
target_compile_definitions(kinu_stream_editor_host PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(kinu_stream_editor_host PRIVATE
  juce::juce_audio_processors
  juce::juce_gui_extra
)
add_dependencies(kinu_stream_editor_host KinuStream_VST3)

# Headless libobs check (manual, not part of ctest): needs /Applications/OBS.app.
#   tests/kinu_stream_obs_harness <kinu-stream-obs.plugin> [seconds] [ppm] [offset-at-s]
if(DAS_BUILD_OBS_PLUGIN)
  add_executable(kinu_stream_obs_harness obs_harness.cpp)
  target_include_directories(kinu_stream_obs_harness PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-source/Source")
  target_include_directories(kinu_stream_obs_harness SYSTEM PRIVATE
    "${obs_headers_SOURCE_DIR}/libobs" "${simde_SOURCE_DIR}")
  target_link_libraries(kinu_stream_obs_harness PRIVATE das_audio_core das_transport "${DAS_OBS_LIBRARY}")
  set_target_properties(kinu_stream_obs_harness PROPERTIES
    BUILD_RPATH "${DAS_OBS_FRAMEWORK_DIR}")
  add_dependencies(kinu_stream_obs_harness kinu_stream_obs)
endif()
