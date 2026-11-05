# SPDX-License-Identifier: AGPL-3.0-only
# Builds the distribution ZIP: "DAS Kai Setup.app" with the three plug-in
# bundles inside, the read-me, licenses and a pointer to the matching source.
# Adapted from DawAudioStreamer's cmake/CreateMacPreviewPackage.cmake
# (yoruhinot): DAS Kai names, Apple Silicon only, build outputs taken directly,
# refuses uncommitted sources, and checks the extracted ZIP before finishing.
#
# Run through the das_kai_package target (see CMakeLists.txt), not by hand.
cmake_minimum_required(VERSION 3.25)

foreach(required DAS_SOURCE_DIR DAS_BINARY_DIR DAS_OUTPUT_DIR DAS_VERSION
                 DAS_VST3 DAS_AU DAS_OBS DAS_SETUP_TESTS
                 DAS_JUCE_DIR DAS_OBS_HEADERS_DIR DAS_SIMDE_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

get_filename_component(source_root "${DAS_SOURCE_DIR}" ABSOLUTE)
get_filename_component(output_root "${DAS_OUTPUT_DIR}" ABSOLUTE)
set(repository "https://github.com/keinoaza666-stack/das-kai")

# The ZIP must match a commit anyone can fetch: SOURCE.txt names it, and the
# AGPL/GPL require the corresponding source to be available.
execute_process(COMMAND git -C "${source_root}" rev-parse HEAD
                OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_result)
# Untracked files count too: a new source file missing from the commit would
# make SOURCE.txt point at code that cannot build this ZIP.
execute_process(COMMAND git -C "${source_root}" status --porcelain
                OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git -C "${source_root}" branch -r --contains "${commit}"
                OUTPUT_VARIABLE remote_branches OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT git_result EQUAL 0 OR commit STREQUAL "")
  message(FATAL_ERROR "Cannot read the current commit")
endif()
# A local trial may skip this with DAS_ALLOW_UNCOMMITTED=1 in the environment;
# the ZIP is then named "-uncommitted" so it is never mistaken for a release.
set(name_suffix "")
set(source_note "")
if(NOT dirty STREQUAL "")
  if(NOT "$ENV{DAS_ALLOW_UNCOMMITTED}" STREQUAL "1")
    message(FATAL_ERROR "Commit your changes first; the ZIP must match a commit.\n${dirty}\n"
                        "(For a local trial only: DAS_ALLOW_UNCOMMITTED=1)")
  endif()
  set(name_suffix "-uncommitted")
  set(source_note "(試験用: このコミットに未コミットの変更を加えて作ったもの / trial build with uncommitted changes)\n")
endif()

set(package_name "DAS-Kai-${DAS_VERSION}-macOS-AppleSilicon${name_suffix}")
set(package_root "${output_root}/${package_name}")
set(setup_app "${package_root}/DAS Kai Setup.app")
set(payload_root "${setup_app}/Contents/Resources/payload")
set(guide_name "README_はじめにお読みください.txt")

string(FIND "${package_root}" "${output_root}/" package_is_inside)
if(NOT package_is_inside EQUAL 0)
  message(FATAL_ERROR "Unsafe package directory: ${package_root}")
endif()

foreach(bundle "${DAS_VST3}" "${DAS_AU}" "${DAS_OBS}")
  if(NOT EXISTS "${bundle}")
    message(FATAL_ERROR "Missing build output: ${bundle}")
  endif()
endforeach()

file(REMOVE_RECURSE "${package_root}")
file(MAKE_DIRECTORY "${payload_root}" "${package_root}/licenses" "${setup_app}/Contents/MacOS")
file(COPY "${DAS_VST3}" "${DAS_AU}" "${DAS_OBS}" DESTINATION "${payload_root}")
configure_file("${source_root}/installer/macos/README-macOS.txt"
               "${package_root}/${guide_name}" COPYONLY)

if(NOT DAS_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
  message(FATAL_ERROR "DAS_VERSION must start with a numeric x.y.z version")
endif()
set(DAS_SETUP_VERSION "${CMAKE_MATCH_1}")
configure_file("${source_root}/installer/macos/Setup-Info.plist.in"
               "${setup_app}/Contents/Info.plist" @ONLY)
# -no-stdlib-rpath: every Swift library is linked by its full /usr/lib/swift
# path, so the default absolute rpath is unused; the release check rejects it.
execute_process(COMMAND /usr/bin/xcrun swiftc -swift-version 5 -O -no-stdlib-rpath
  -target arm64-apple-macosx13.0 -framework AppKit
  "${source_root}/installer/macos/SetupCore.swift"
  "${source_root}/installer/macos/SetupApp.swift"
  -o "${setup_app}/Contents/MacOS/DASKaiSetup"
  RESULT_VARIABLE setup_build_result)
if(NOT setup_build_result EQUAL 0)
  message(FATAL_ERROR "Failed to build DAS Kai Setup.app")
endif()

# Licenses: the fork's summary and full texts, the upstream notices, and the
# third-party texts the binaries are built with.
file(COPY "${source_root}/LICENSE" "${source_root}/THIRD_PARTY_NOTICES.md"
          "${source_root}/MODIFICATIONS.md"
     DESTINATION "${package_root}/licenses")
file(COPY "${source_root}/LICENSES/" DESTINATION "${package_root}/licenses/LICENSES")
configure_file("${source_root}/libs/transport/LICENSE"
               "${package_root}/licenses/MIT-transport.txt" COPYONLY)
configure_file("${source_root}/libs/sync/LICENSE"
               "${package_root}/licenses/MIT-sync.txt" COPYONLY)
# JUCE's LICENSE.md only links to the texts of the libraries it embeds in the
# binaries (HarfBuzz, SheenBidi, libpng, IJG JPEG, zlib); ship the texts too.
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
  file(COPY "${license_file}" DESTINATION "${package_root}/licenses/${folder}")
endforeach()

file(WRITE "${package_root}/SOURCE.txt"
"このリリースに対応するソース:\n"
"${repository}/tree/${commit}\n${source_note}\n"
"DAS Kai は DawAudioStreamer(yoruhinot 作、https://github.com/yoruhinot/DawAudioStreamer)を\n"
"改変したものです。変更点は licenses/MODIFICATIONS.md、ライセンス条件は licenses 内を参照してください。\n\n"
"---\n\n"
"Source for this release:\n"
"${repository}/tree/${commit}\n${source_note}\n"
"DAS Kai is a modified version of DawAudioStreamer by yoruhinot. See licenses/MODIFICATIONS.md\n"
"for the changes and the licenses folder for the license terms.\n")

# Ad-hoc signatures (no Apple Developer ID): they let Setup detect damaged
# files, not prove who built them. Same as the original's macOS preview.
foreach(bundle
    "${payload_root}/DAS Kai.vst3"
    "${payload_root}/DAS Kai.component"
    "${payload_root}/das-kai-obs.plugin"
    "${setup_app}")
  execute_process(COMMAND /usr/bin/codesign --force --deep --sign - "${bundle}"
                  RESULT_VARIABLE sign_result)
  if(NOT sign_result EQUAL 0)
    message(FATAL_ERROR "Ad-hoc signing failed: ${bundle}")
  endif()
  execute_process(COMMAND /usr/bin/codesign --verify --deep --strict "${bundle}"
                  RESULT_VARIABLE verify_result)
  if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "Code signature verification failed: ${bundle}")
  endif()
endforeach()

set(archive "${output_root}/${package_name}.zip")
file(REMOVE "${archive}")
# No extended attributes: files made on this Mac carry com.apple.provenance,
# which would add a __MACOSX/._* entry for every file (upstream builds on CI
# and has none). Code signatures do not depend on them; checked below.
execute_process(
  COMMAND /usr/bin/ditto -c -k --norsrc --noextattr --noacl --keepParent
          "${package_root}" "${archive}"
  RESULT_VARIABLE archive_result)
if(NOT archive_result EQUAL 0 OR NOT EXISTS "${archive}")
  message(FATAL_ERROR "Failed to create ${archive}")
endif()

# Check what a user gets: extract the ZIP and run Setup's install, reinstall
# and uninstall on the extracted payload in a throw-away home folder.
set(check_root "${output_root}/zip-check")
file(REMOVE_RECURSE "${check_root}")
file(MAKE_DIRECTORY "${check_root}")
execute_process(COMMAND /usr/bin/ditto -x -k "${archive}" "${check_root}"
                RESULT_VARIABLE extract_result)
set(extracted "${check_root}/${package_name}")
if(NOT extract_result EQUAL 0 OR NOT EXISTS "${extracted}/${guide_name}")
  message(FATAL_ERROR "The ZIP does not extract to the expected folder")
endif()
execute_process(COMMAND /usr/bin/codesign --verify --deep --strict
                        "${extracted}/DAS Kai Setup.app"
                RESULT_VARIABLE extracted_sign_result)
if(NOT extracted_sign_result EQUAL 0)
  message(FATAL_ERROR "Setup.app signature broke inside the ZIP")
endif()
execute_process(COMMAND "${DAS_SETUP_TESTS}"
                        "${extracted}/DAS Kai Setup.app/Contents/Resources/payload"
                RESULT_VARIABLE payload_test_result)
if(NOT payload_test_result EQUAL 0)
  message(FATAL_ERROR "Setup tests failed on the extracted ZIP")
endif()
file(REMOVE_RECURSE "${check_root}")

file(SHA256 "${archive}" archive_sha)
file(WRITE "${output_root}/${package_name}.sha256" "${archive_sha}  ${package_name}.zip\n")
message(STATUS "Created ${archive}")
message(STATUS "SHA-256 ${archive_sha}")
message(STATUS "Source  ${repository}/tree/${commit}")
if(NOT remote_branches MATCHES "origin/")
  message(WARNING "Commit ${commit} is not on GitHub yet: push it before publishing this ZIP.")
endif()
