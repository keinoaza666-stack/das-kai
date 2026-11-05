if(WIN32)
  set(das_transport_platform_sources src/NamedSharedMemory_win.cpp)
else()
  set(das_transport_platform_sources src/NamedSharedMemory_posix.cpp)
endif()

add_library(das_transport STATIC
  src/RingBuffer.cpp
  src/MonotonicClock.cpp
  ${das_transport_platform_sources}
)
set_target_properties(das_transport PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(das_transport PUBLIC include)
target_compile_features(das_transport PUBLIC cxx_std_20)
