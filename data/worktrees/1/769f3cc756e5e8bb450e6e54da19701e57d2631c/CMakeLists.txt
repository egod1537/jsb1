cmake_minimum_required(VERSION 3.28)

project(
  jsb-flight-console
  VERSION 0.1.0
  LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(BuildOptions)
include(Dependencies)
include(Contract)

add_subdirectory(src/common)
if(JSB_BUILD_EDITOR)
  include(ImGui)
  include(ImPlot)
  add_subdirectory(src/flightui)
endif()
add_subdirectory(src/contract)
add_subdirectory(src/sim)
add_subdirectory(src/messaging)
add_subdirectory(src/gui)
if(JSB_BUILD_EDITOR)
  add_subdirectory(src/integration)
  add_subdirectory(src/app)
endif()
add_subdirectory(src/runner)

enable_testing()
add_subdirectory(tests)
