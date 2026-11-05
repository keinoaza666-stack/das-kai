# Checks promises about DAS Kai Setup that span several files.
# Run by ctest (setup_contract); ROOT is the source folder.
cmake_minimum_required(VERSION 3.25)

set(failures "")
macro(fail message)
  string(APPEND failures "  - ${message}\n")
endmacro()

# 1. Like the original DawAudioStreamer Setup: no privilege requests and no
#    changes to permissions or Gatekeeper attributes. A user approves Setup
#    in macOS's own dialog; Setup must not work around that.
foreach(name SetupApp.swift SetupCore.swift)
  file(READ "${ROOT}/installer/macos/${name}" text)
  if(text MATCHES "(^|[^A-Za-z])(sudo|chmod|chown|xattr|spctl|AuthorizationCreate)([^A-Za-z]|$)")
    fail("${name} uses ${CMAKE_MATCH_2}")
  endif()
endforeach()

# 2. The same three bundle names everywhere they are written by hand. The
#    packaging step separately checks them against the real build outputs.
foreach(file installer/macos/SetupCore.swift cmake/CreateMacPackage.cmake
             scripts/install.sh installer/macos/README-macOS.txt README.md)
  file(READ "${ROOT}/${file}" text)
  foreach(bundle "DAS Kai.vst3" "DAS Kai.component" "das-kai-obs.plugin")
    string(FIND "${text}" "${bundle}" found)
    if(found EQUAL -1)
      fail("${file} does not name ${bundle}")
    endif()
  endforeach()
endforeach()

# 3. One repository for help, reports and the source pointer. The URL must end
#    there (das-kai2 or das-kai-old would be another repository), and the app
#    must not send people to the original project's site.
set(repository "https://github.com/keinoaza666-stack/das-kai")
string(REPLACE "." "\\." repository_pattern "${repository}")
foreach(file installer/macos/SetupApp.swift cmake/CreateMacPackage.cmake
             installer/macos/README-macOS.txt)
  file(READ "${ROOT}/${file}" text)
  if(NOT text MATCHES "${repository_pattern}([^A-Za-z0-9_.-]|$)")
    fail("${file} does not point to ${repository}")
  endif()
  string(REGEX MATCHALL "https://github\\.com/keinoaza666-stack/[A-Za-z0-9_.-]+" urls "${text}")
  foreach(url IN LISTS urls)
    if(NOT url STREQUAL repository)
      fail("${file} points to another repository: ${url}")
    endif()
  endforeach()
endforeach()
file(READ "${ROOT}/installer/macos/SetupApp.swift" app)
if(app MATCHES "yoruhinot\\.github\\.io|github\\.com/yoruhinot")
  fail("SetupApp.swift links to the original project")
endif()

# 4. Work folders and the lock must differ from the original Setup's, or each
#    Setup would refuse to run after the other left recovery files.
file(READ "${ROOT}/installer/macos/SetupCore.swift" core)
if(core MATCHES "\\.das-setup-|\\.das-probe-|org\\.dawaudiostreamer")
  fail("SetupCore.swift reuses the original Setup's work-folder or lock names")
endif()

if(NOT failures STREQUAL "")
  message(FATAL_ERROR "Setup contract broken:\n${failures}")
endif()
message(STATUS "Setup contract: OK")
