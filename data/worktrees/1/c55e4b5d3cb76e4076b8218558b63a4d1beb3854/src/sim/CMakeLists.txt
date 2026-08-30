add_library(jsb_sim_core STATIC
  telemetry/TelemetryContracts.cpp
  telemetry/TelemetryChannel.cpp
  telemetry/TelemetryRegistry.cpp
  telemetry/recording/TelemetryRecordingService.cpp
  Aircraft.cpp
  linearization/AsyncAircraftLinearizer.cpp
  linearization/AircraftLinearizer.cpp
  linearization/DynamicModeAnalyzer.cpp
  linearization/DynamicModeHistory.cpp
  linearization/LinearizationResult.cpp
  linearization/LinearizationPerturbation.cpp
  linearization/LinearizationStateVector.cpp
  Component.cpp
  ErrorTracker.cpp
  execution/ExecutionVariant.cpp
  execution/ExecutionVariantResolver.cpp
  InitialCondition.cpp
  StateLogger.cpp
  scenario/SimulationScenario.cpp
  scenario/SimulationScenarioSerializer.cpp
  control/ManualFlightControlController.cpp
  control/FlightControlManager.cpp
  gnc/TrimSolver.cpp
  gnc/TrimService.cpp
  gnc/autopilot/MyAutopilot.cpp
  gnc/autopilot/PX4Autopilot.cpp
  gnc/autopilot/AutopilotFactory.cpp
  gnc/hold/RollHoldController.cpp
  gnc/hold/Px4RollHoldReferenceController.cpp
  gnc/hold/PitchHoldController.cpp
  gnc/hold/AirspeedHoldController.cpp
  gnc/hold/CourseHoldController.cpp
  gnc/hold/YawDamperController.cpp
  gnc/hold/AltitudeHoldController.cpp
  scenario/ScenarioExecutor.cpp
  jsbsim/ControlSystem.cpp
  jsbsim/Properties.cpp
  jsbsim/EngineSystem.cpp
  jsbsim/FDMStateAccess.cpp
  Simulation.cpp
  gnc/filters/WashoutFilter.cpp
)

target_compile_features(jsb_sim_core PUBLIC cxx_std_20)
target_precompile_headers(jsb_sim_core PRIVATE
  <memory>
  <optional>
  <string>
  <utility>
  <vector>
)
set_target_properties(jsb_sim_core PROPERTIES CXX_SCAN_FOR_MODULES OFF)
if(JSB_ENABLE_UNITY_BUILD)
  set_target_properties(jsb_sim_core PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 4
  )
endif()
target_include_directories(jsb_sim_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(jsb_sim_core
  PUBLIC
    jsb_common
    libJSBSim
    Eigen3::Eigen
  PRIVATE
    jsb_contract
    yaml-cpp::yaml-cpp
)

add_library(jsb_sim_runtime STATIC
  runtime/SimulationComparison.cpp
  runtime/SimulationRuntime.cpp
)
target_compile_features(jsb_sim_runtime PUBLIC cxx_std_20)
set_target_properties(jsb_sim_runtime PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_include_directories(jsb_sim_runtime PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(jsb_sim_runtime PUBLIC jsb_sim_core)
