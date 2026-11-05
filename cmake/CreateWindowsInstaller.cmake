# SPDX-License-Identifier: AGPL-3.0-only
# Builds the Windows installer: stages the VST3, the OBS plug-in, the read-me,
# licenses and a pointer to the matching source into one folder, then runs
# Inno Setup on installer/windows/DASKai.iss. Added for DAS Kai (2026-11-05),
# following the macOS package (cmake/CreateMacPackage.cmake).
#
# Run through the das_kai_package target (see CMakeLists.txt), not by hand.
cmake_minimum_required(VERSION 3.25)

foreach(required DAS_SOURCE_DIR DAS_OUTPUT_DIR DAS_VERSION DAS_ISCC
                 DAS_VST3 DAS_OBS_DLL DAS_JUCE_DIR DAS_OBS_HEADERS_DIR DAS_SIMDE_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

get_filename_component(source_root "${DAS_SOURCE_DIR}" ABSOLUTE)
get_filename_component(output_root "${DAS_OUTPUT_DIR}" ABSOLUTE)
get_filename_component(vst3_bundle "${DAS_VST3}" ABSOLUTE)
set(repository "https://github.com/keinoaza666-stack/das-kai")

# The installer must match a commit anyone can fetch (SOURCE.txt names it).
execute_process(COMMAND git -C "${source_root}" rev-parse HEAD
                OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_result)
execute_process(COMMAND git -C "${source_root}" status --porcelain
                OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT git_result EQUAL 0 OR commit STREQUAL "")
  message(FATAL_ERROR "Cannot read the current commit")
endif()
set(name_suffix "")
set(source_note "")
if(NOT dirty STREQUAL "")
  if(NOT "$ENV{DAS_ALLOW_UNCOMMITTED}" STREQUAL "1")
    message(FATAL_ERROR "Commit your changes first; the installer must match a commit.\n${dirty}\n"
                        "(For a local trial only: DAS_ALLOW_UNCOMMITTED=1)")
  endif()
  set(name_suffix "-uncommitted")
  set(source_note "(試験用: このコミットに未コミットの変更を加えて作ったもの / trial build with uncommitted changes)\n")
endif()

set(stage "${output_root}/windows-stage")
string(FIND "${stage}" "${output_root}/" stage_is_inside)
if(NOT stage_is_inside EQUAL 0)
  message(FATAL_ERROR "Unsafe stage directory: ${stage}")
endif()
foreach(input "${vst3_bundle}" "${DAS_OBS_DLL}")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "Missing build output: ${input}")
  endif()
endforeach()
get_filename_component(vst3_name "${vst3_bundle}" NAME)
if(NOT vst3_name STREQUAL "DAS Kai.vst3")
  message(FATAL_ERROR "Unexpected VST3 bundle: ${vst3_bundle}")
endif()

file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${stage}/obs/das-kai-obs/bin/64bit" "${stage}/obs/das-kai-obs/data/locale"
                    "${stage}/docs" "${stage}/licenses")
file(COPY "${vst3_bundle}" DESTINATION "${stage}")
file(COPY "${DAS_OBS_DLL}" DESTINATION "${stage}/obs/das-kai-obs/bin/64bit")
file(COPY "${source_root}/plugins/obs-source/data/locale/ja-JP.ini"
          "${source_root}/plugins/obs-source/data/locale/en-US.ini"
     DESTINATION "${stage}/obs/das-kai-obs/data/locale")
configure_file("${source_root}/installer/windows/README-Windows.txt"
               "${stage}/docs/README_はじめにお読みください.txt" COPYONLY)

# Licenses: the same set as the macOS ZIP, plus Inno Setup's (its code runs
# as the installer and uninstaller).
file(COPY "${source_root}/LICENSE" "${source_root}/THIRD_PARTY_NOTICES.md"
          "${source_root}/MODIFICATIONS.md"
     DESTINATION "${stage}/licenses")
file(COPY "${source_root}/LICENSES/" DESTINATION "${stage}/licenses/LICENSES")
configure_file("${source_root}/libs/transport/LICENSE" "${stage}/licenses/MIT-transport.txt" COPYONLY)
configure_file("${source_root}/libs/sync/LICENSE" "${stage}/licenses/MIT-sync.txt" COPYONLY)
set(juce_modules "${DAS_JUCE_DIR}/modules")
foreach(pair
    "${DAS_JUCE_DIR}/LICENSE.md|JUCE"
    "${juce_modules}/juce_graphics/fonts/harfbuzz/COPYING|JUCE/HarfBuzz"
    "${juce_modules}/juce_graphics/unicode/sheenbidi/LICENSE|JUCE/SheenBidi"
    "${juce_modules}/juce_graphics/image_formats/pnglib/LICENSE|JUCE/libpng"
    "${juce_modules}/juce_graphics/image_formats/jpglib/README|JUCE/IJG-JPEG"
    "${juce_modules}/juce_core/zip/zlib/README|JUCE/zlib"
    "${juce_modules}/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt|VST3-SDK"
    "${DAS_OBS_HEADERS_DIR}/COPYING|OBS"
    "${DAS_SIMDE_DIR}/COPYING|SIMDe")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 license_file)
  list(GET parts 1 folder)
  if(NOT EXISTS "${license_file}")
    message(FATAL_ERROR "Missing third-party license: ${license_file}")
  endif()
  file(COPY "${license_file}" DESTINATION "${stage}/licenses/${folder}")
endforeach()

file(WRITE "${stage}/docs/SOURCE.txt"
"このリリースに対応するソース:\n"
"${repository}/tree/${commit}\n${source_note}\n"
"DAS Kai は DawAudioStreamer(yoruhinot 作、https://github.com/yoruhinot/DawAudioStreamer)を\n"
"改変したものです。変更点は licenses/MODIFICATIONS.md、ライセンス条件は licenses 内を参照してください。\n\n"
"---\n\n"
"Source for this release:\n"
"${repository}/tree/${commit}\n${source_note}\n"
"DAS Kai is a modified version of DawAudioStreamer by yoruhinot. See licenses/MODIFICATIONS.md\n"
"for the changes and the licenses folder for the license terms.\n")

file(TO_NATIVE_PATH "${stage}" stage_native)
file(TO_NATIVE_PATH "${output_root}" output_native)
execute_process(
  COMMAND "${DAS_ISCC}" /Qp
          "/DAppVersion=${DAS_VERSION}"
          "/DStageDir=${stage_native}"
          "/DOutputDir=${output_native}"
          "${source_root}/installer/windows/DASKai.iss"
  RESULT_VARIABLE iscc_result)
set(setup "${output_root}/DAS-Kai-${DAS_VERSION}-Windows-x64-Setup.exe")
if(NOT iscc_result EQUAL 0 OR NOT EXISTS "${setup}")
  message(FATAL_ERROR "Inno Setup failed (${iscc_result})")
endif()
if(NOT name_suffix STREQUAL "")
  set(renamed "${output_root}/DAS-Kai-${DAS_VERSION}-Windows-x64-Setup${name_suffix}.exe")
  file(RENAME "${setup}" "${renamed}")
  set(setup "${renamed}")
endif()

get_filename_component(setup_name "${setup}" NAME)
file(SHA256 "${setup}" setup_sha)
# Named like the macOS one: DAS-Kai-<version>-Windows-x64-Setup.sha256
string(REGEX REPLACE "\\.exe$" ".sha256" sha_file "${setup}")
# LF line end: on Windows file(WRITE) gave CRLF, and `shasum -c` on a Mac
# then looked for "<name>\r" (found in the 0.2.0 release).
file(CONFIGURE OUTPUT "${sha_file}" CONTENT "${setup_sha}  ${setup_name}\n" NEWLINE_STYLE UNIX)
message(STATUS "Created ${setup}")
message(STATUS "SHA-256 ${setup_sha}")
message(STATUS "Source  ${repository}/tree/${commit}")
