add_library(jsb_common STATIC
  crypto/Sha256.cpp
  random/Rng.cpp
)
target_compile_features(jsb_common PUBLIC cxx_std_20)
set_target_properties(jsb_common PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_include_directories(jsb_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
