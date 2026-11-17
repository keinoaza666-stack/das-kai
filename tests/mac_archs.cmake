# SPDX-License-Identifier: MIT
# Added for DAS Kai (2026-11-17). Checks which CPU architectures the built
# plug-ins hold:
#   VST3 and AU: arm64 and x86_64. A DAW that an Apple Silicon Mac opens with
#     Rosetta only loads x86_64 code; an arm64-only build was put on Cubase 14's
#     blocklist.
#   OBS plug-in: arm64 only (OBS.app's libobs is arm64 only).
# Usage: cmake -DVST3=<binary> -DAU=<binary> [-DOBS=<binary>] -P mac_archs.cmake
function(check_archs label binary expected)
  if(NOT EXISTS "${binary}")
    message(FATAL_ERROR "${label}: missing ${binary}")
  endif()
  execute_process(COMMAND /usr/bin/lipo -archs "${binary}"
                  OUTPUT_VARIABLE archs OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label}: lipo failed for ${binary}")
  endif()
  string(REPLACE " " ";" found "${archs}")
  list(SORT found)
  set(wanted ${expected})
  list(SORT wanted)
  if(NOT "${found}" STREQUAL "${wanted}")
    message(FATAL_ERROR "${label}: has '${archs}', expected '${expected}'. "
            "An old build folder keeps its architecture in the cache: reconfigure "
            "with -DCMAKE_OSX_ARCHITECTURES=\"arm64;x86_64\".")
  endif()
  message(STATUS "${label}: ${archs}")
endfunction()

check_archs("VST3" "${VST3}" "arm64;x86_64")
check_archs("AU" "${AU}" "arm64;x86_64")
if(NOT "${OBS}" STREQUAL "")
  check_archs("OBS plug-in" "${OBS}" "arm64")
endif()
