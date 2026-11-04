add_library(kinu_stream_obs MODULE Source/obs-plugin.cpp)
target_include_directories(kinu_stream_obs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# Third-party headers: SYSTEM keeps their pedantic warnings out of our build.
target_include_directories(kinu_stream_obs SYSTEM PRIVATE
  "${obs_headers_SOURCE_DIR}/libobs"
  "${simde_SOURCE_DIR}"
)
target_link_libraries(kinu_stream_obs PRIVATE das_sync das_transport)
set_target_properties(kinu_stream_obs PROPERTIES
  PREFIX ""
  OUTPUT_NAME "kinu-stream-obs"
  # No absolute rpath: @rpath/libobs resolves through OBS.app's own
  # @executable_path/../Frameworks, exactly like upstream's installed plug-in.
  SKIP_BUILD_RPATH ON
)

set(DAS_OBS_FRAMEWORK_DIR "/Applications/OBS.app/Contents/Frameworks" CACHE PATH
    "Directory containing libobs.framework")
find_library(DAS_OBS_LIBRARY NAMES libobs
  PATHS "${DAS_OBS_FRAMEWORK_DIR}"
  NO_DEFAULT_PATH
  REQUIRED)
target_link_libraries(kinu_stream_obs PRIVATE "${DAS_OBS_LIBRARY}")
target_sources(kinu_stream_obs PRIVATE data/locale/ja-JP.ini data/locale/en-US.ini)
set_source_files_properties(data/locale/ja-JP.ini data/locale/en-US.ini PROPERTIES
  MACOSX_PACKAGE_LOCATION "Resources/locale")
set_target_properties(kinu_stream_obs PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION plugin
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.keinoaza.kinustream.obs"
  MACOSX_BUNDLE_BUNDLE_NAME "Kinu Stream"
  MACOSX_BUNDLE_BUNDLE_VERSION "${DAS_COMPONENT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${DAS_COMPONENT_VERSION}"
)
