add_library(das_sync STATIC
  src/FrameClockEstimator.cpp
  src/SincInterpolator.cpp
  src/StreamReceiver.cpp
)
set_target_properties(das_sync PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(das_sync PUBLIC include)
target_link_libraries(das_sync PUBLIC das_transport)
target_compile_features(das_sync PUBLIC cxx_std_20)
