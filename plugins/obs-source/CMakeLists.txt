add_library(das_kai_obs MODULE Source/obs-plugin.cpp)
target_include_directories(das_kai_obs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# Third-party headers: SYSTEM keeps their pedantic warnings out of our build.
target_include_directories(das_kai_obs SYSTEM PRIVATE
  "${obs_headers_SOURCE_DIR}/libobs"
  "${simde_SOURCE_DIR}"
)
target_link_libraries(das_kai_obs PRIVATE das_sync das_transport)
set_target_properties(das_kai_obs PROPERTIES
  PREFIX ""
  OUTPUT_NAME "das-kai-obs"
  # No absolute rpath: @rpath/libobs resolves through OBS.app's own
  # @executable_path/../Frameworks, exactly like upstream's installed plug-in.
  SKIP_BUILD_RPATH ON
)

if(WIN32)
  # Windows (2026-11-05, from upstream): link against obs.dll through an import
  # library made from obs-import.def; OBS loads the DLL from
  # %ProgramData%\obs-studio\plugins\das-kai-obs\bin\64bit.
  set(obs_import_library "${CMAKE_CURRENT_BINARY_DIR}/obs.lib")
  add_custom_command(
    OUTPUT "${obs_import_library}"
    COMMAND "${CMAKE_AR}" /nologo /machine:x64
            "/def:${CMAKE_CURRENT_SOURCE_DIR}/obs-import.def"
            "/out:${obs_import_library}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/obs-import.def"
    VERBATIM
  )
  add_custom_target(das_kai_obs_import_library DEPENDS "${obs_import_library}")
  configure_file(Source/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
  target_sources(das_kai_obs PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
  add_dependencies(das_kai_obs das_kai_obs_import_library)
  target_link_libraries(das_kai_obs PRIVATE "${obs_import_library}")
  return()
endif()

set(DAS_OBS_FRAMEWORK_DIR "/Applications/OBS.app/Contents/Frameworks" CACHE PATH
    "Directory containing libobs.framework")
find_library(DAS_OBS_LIBRARY NAMES libobs
  PATHS "${DAS_OBS_FRAMEWORK_DIR}"
  NO_DEFAULT_PATH
  REQUIRED)
target_link_libraries(das_kai_obs PRIVATE "${DAS_OBS_LIBRARY}")
target_sources(das_kai_obs PRIVATE data/locale/ja-JP.ini data/locale/en-US.ini)
set_source_files_properties(data/locale/ja-JP.ini data/locale/en-US.ini PROPERTIES
  MACOSX_PACKAGE_LOCATION "Resources/locale")
set_target_properties(das_kai_obs PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION plugin
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.keinoaza.kinustream.obs"
  MACOSX_BUNDLE_BUNDLE_NAME "DAS Kai"
  MACOSX_BUNDLE_BUNDLE_VERSION "${DAS_COMPONENT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${DAS_COMPONENT_VERSION}"
)
