function(add_jsb_test target source)
  add_executable(${target}
    ${source}
  )

  target_compile_features(${target} PRIVATE
    cxx_std_20
  )

  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
  )

  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )

  if(ARGN)
    target_link_libraries(${target} PRIVATE
      ${ARGN}
    )
  endif()

  add_test(
    NAME ${target}
    COMMAND ${target}
  )
endfunction()

add_jsb_test(math_tests
  MathTests.cpp
)

add_jsb_test(gui_architecture_tests
  GuiArchitectureTests.cpp
  jsb_gui_architecture
)

add_jsb_test(contract_runtime_tests
  ContractRuntimeTests.cpp
  jsb_sim_core
  jsb_contract_proto
)
target_compile_definitions(contract_runtime_tests PRIVATE
  JSB_TEST_CONTRACT_SCENARIO_PATH="${PROJECT_SOURCE_DIR}/contract/examples/scenario/roll_hold.yaml"
  JSB_TEST_EXECUTION_VARIANTS_PATH="${PROJECT_SOURCE_DIR}/contract/execution/variants.json"
  JSB_TEST_EXECUTION_CAPABILITIES_PATH="${PROJECT_SOURCE_DIR}/contract/execution/capabilities.json"
)

add_test(
  NAME contract_validation
  COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/scripts/contract_tool.py"
    validate
    --root "${PROJECT_SOURCE_DIR}"
    --protoc "$<TARGET_FILE:protobuf::protoc>"
    --output "${PROJECT_BINARY_DIR}/test-results/contract-validation"
)

add_test(
  NAME architecture_boundaries
  COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/scripts/check_architecture.py"
    --root "${PROJECT_SOURCE_DIR}"
)

set(JSB_CONTRACT_RUN_OUTPUT
  "${PROJECT_BINARY_DIR}/test-results/contract-roll-hold"
)
add_test(
  NAME contract_roll_hold_run
  COMMAND jsb-sim-runner
    --scenario "${PROJECT_SOURCE_DIR}/contract/examples/scenario/roll_hold.yaml"
    --output "${JSB_CONTRACT_RUN_OUTPUT}"
)
set_tests_properties(contract_roll_hold_run PROPERTIES
  FIXTURES_SETUP contract_roll_hold_artifacts
)
add_test(
  NAME contract_run_metadata_validation
  COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/scripts/contract_tool.py"
    validate-run
    --root "${PROJECT_SOURCE_DIR}"
    --run-json "${JSB_CONTRACT_RUN_OUTPUT}/run.json"
)
set_tests_properties(contract_run_metadata_validation PROPERTIES
  FIXTURES_REQUIRED contract_roll_hold_artifacts
)

add_jsb_test(monitor_controller_tests
  MonitorControllerTests.cpp
  jsb_monitor
)

if(JSB_BUILD_EDITOR)
  add_jsb_test(flightui_tests
    FlightUITests.cpp
    flightui
  )
endif()

if(JSB_BUILD_EDITOR)
  add_jsb_test(linearization_value_transform_tests
    LinearizationValueTransformTests.cpp
    jsb_editor
  )
endif()

add_jsb_test(simulation_tests
  SimulationTests.cpp
  jsb_sim_core
)

if(JSB_BUILD_EDITOR)
  add_jsb_test(scenario_tests
    ScenarioTests.cpp
    jsb_editor
  )
endif()

if(JSB_BUILD_EDITOR)
  add_jsb_test(local_view_state_tests
    LocalViewStateTests.cpp
    jsb_editor
  )
endif()

if(JSB_BUILD_EDITOR)
  add_jsb_test(autopilot_selector_feature_tests
    AutopilotSelectorFeatureTests.cpp
    jsb_editor
  )
endif()

if(JSB_BUILD_EDITOR)
  add_jsb_test(gui_feature_controller_tests
    GuiFeatureControllerTests.cpp
    jsb_editor
  )
endif()

add_jsb_test(scenario_execution_tests
  ScenarioExecutionTests.cpp
  jsb_messaging
)

add_jsb_test(telemetry_tests
  TelemetryTests.cpp
  jsb_sim_core
)

add_jsb_test(message_bus_tests
  MessageBusTests.cpp
  jsb_message_bus
)

add_jsb_test(message_bus_simulation_integration_tests
  MessageBusSimulationIntegrationTests.cpp
  jsb_messaging
)

add_jsb_test(mcap_recording_tests
  McapRecordingTests.cpp
  jsb_sim_core
)

target_compile_definitions(mcap_recording_tests PRIVATE
  JSB_TEST_SAMPLE_MCAP_PATH="${PROJECT_BINARY_DIR}/test-results/telemetry.mcap"
)

add_jsb_test(runner_mcap_integration_tests
  RunnerMcapIntegrationTests.cpp
  jsb_runner
  jsb_contract_proto
)

target_compile_definitions(runner_mcap_integration_tests PRIVATE
  JSB_TEST_HEADLESS_SCENARIO_PATH="${PROJECT_SOURCE_DIR}/tests/data/scenarios/headless_smoke.yaml"
  JSB_TEST_CANONICAL_SCENARIO_PATH="${PROJECT_SOURCE_DIR}/scenarios/roll_hold_5deg_30s.yaml"
)

add_jsb_test(linearization_perturbation_tests
  LinearizationPerturbationTests.cpp
  jsb_sim_core
)

add_jsb_test(linearization_state_vector_tests
  LinearizationStateVectorTests.cpp
  jsb_sim_core
)

add_jsb_test(dynamic_mode_analyzer_tests
  DynamicModeAnalyzerTests.cpp
  jsb_sim_core
)

if(JSB_BUILD_EDITOR)
  add_jsb_test(flight_camera_controller_tests
    FlightCameraControllerTests.cpp
    jsb_editor
  )
endif()

if(JSB_BUILD_EDITOR)
  add_jsb_test(flight_path_history_tests
    FlightPathHistoryTests.cpp
    jsb_editor
  )
endif()

if(JSB_BUILD_EDITOR)
  add_jsb_test(editor_icon_registry_tests
    EditorIconRegistryTests.cpp
    jsb_editor
  )
endif()

if(JSB_BUILD_EDITOR)
  add_jsb_test(editor_layout_manager_tests
    EditorLayoutManagerTests.cpp
    jsb_editor
  )
endif()

add_executable(px4_roll_tuning_probe
  Px4RollTuningProbe.cpp
)

target_compile_features(px4_roll_tuning_probe PRIVATE
  cxx_std_20
)

set_target_properties(px4_roll_tuning_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

target_include_directories(px4_roll_tuning_probe PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(px4_roll_tuning_probe PRIVATE
  jsb_sim_core
)

if(TARGET jsb-sim-runner)
  add_test(
    NAME headless_runner_help
    COMMAND jsb-sim-runner --help
  )
  set_tests_properties(headless_runner_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage:"
  )

  add_test(
    NAME headless_runner_invalid_arguments
    COMMAND jsb-sim-runner --scenario "${PROJECT_SOURCE_DIR}/tests/data/scenarios/headless_smoke.yaml"
  )
  set_tests_properties(headless_runner_invalid_arguments PROPERTIES
    WILL_FAIL TRUE
  )

  add_test(
    NAME headless_runner_smoke
    COMMAND ${CMAKE_COMMAND}
      -DRUNNER=$<TARGET_FILE:jsb-sim-runner>
      -DSCENARIO=${PROJECT_SOURCE_DIR}/tests/data/scenarios/headless_smoke.yaml
      -DOUTPUT=${PROJECT_BINARY_DIR}/test-results/headless-smoke
      -P ${PROJECT_SOURCE_DIR}/tests/VerifyHeadlessRunner.cmake
  )
endif()
