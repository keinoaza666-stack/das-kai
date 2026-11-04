add_library(das_transport STATIC
  src/RingBuffer.cpp
  src/NamedSharedMemory_posix.cpp
)
set_target_properties(das_transport PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(das_transport PUBLIC include)
target_compile_features(das_transport PUBLIC cxx_std_20)
