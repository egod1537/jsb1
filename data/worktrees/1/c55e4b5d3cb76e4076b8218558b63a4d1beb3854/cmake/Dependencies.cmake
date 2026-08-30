include(FetchContent)

FetchContent_Declare(
  jsbsim
  GIT_REPOSITORY https://github.com/JSBSim-Team/jsbsim.git
  GIT_TAG v1.3.0
)
FetchContent_Declare(
  eigen
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG 5.0.1
)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG yaml-cpp-0.9.0
)
FetchContent_Declare(
  mcap
  GIT_REPOSITORY https://github.com/foxglove/mcap.git
  GIT_TAG releases/cpp/v2.1.3
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG v21.12
  GIT_SHALLOW TRUE
)

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(jsbsim eigen yaml-cpp mcap protobuf)

add_library(mcap_cpp INTERFACE)
target_include_directories(mcap_cpp INTERFACE
  ${mcap_SOURCE_DIR}/cpp/mcap/include
)
target_compile_definitions(mcap_cpp INTERFACE
  MCAP_COMPRESSION_NO_LZ4
  MCAP_COMPRESSION_NO_ZSTD
  MCAP_PUBLIC=
)

if(JSB_BUILD_EDITOR)
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
  )
  FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v1.0
  )
  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  )
  FetchContent_MakeAvailable(imgui implot stb)
  find_package(OpenGL REQUIRED)
  find_package(glfw3 REQUIRED)
endif()
