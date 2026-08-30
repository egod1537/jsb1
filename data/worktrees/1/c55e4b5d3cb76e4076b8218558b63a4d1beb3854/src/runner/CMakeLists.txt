add_library(jsb_runner STATIC
  McapRunObserver.cpp
  RunnerOptions.cpp
  SimulationRunner.cpp
)

target_compile_features(jsb_runner PUBLIC cxx_std_20)
target_include_directories(jsb_runner PUBLIC
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(jsb_runner
  PUBLIC jsb_sim_runtime
  PRIVATE jsb_contract
)
target_compile_definitions(jsb_runner PRIVATE
  JSB_APPLICATION_VERSION="${PROJECT_VERSION}"
  JSB_GIT_COMMIT="${JSB_GIT_COMMIT}"
  JSB_RUNTIME_BRANCH="${JSB_GIT_BRANCH}"
  JSB_CONTRACT_VERSION="${JSB_CONTRACT_VERSION}"
  JSB_TELEMETRY_SCHEMA_VERSION=${JSB_TELEMETRY_SCHEMA_VERSION}
)

add_executable(jsb-sim-runner main.cpp)
target_compile_features(jsb-sim-runner PRIVATE cxx_std_20)
set_target_properties(jsb-sim-runner PROPERTIES
  OUTPUT_NAME jsb0-runner
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)
target_link_libraries(jsb-sim-runner PRIVATE jsb_runner)
add_custom_command(TARGET jsb-sim-runner POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:jsb-sim-runner>"
    "$<TARGET_FILE_DIR:jsb-sim-runner>/jsb-sim-runner$<TARGET_FILE_SUFFIX:jsb-sim-runner>"
  COMMENT "Providing the legacy jsb-sim-runner executable name"
)
