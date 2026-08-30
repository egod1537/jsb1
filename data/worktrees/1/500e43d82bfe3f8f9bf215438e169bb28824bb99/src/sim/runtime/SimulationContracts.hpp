#pragma once

#include "common/math/Math.hpp"
#include "contract/telemetry/RecordingTypes.hpp"
#include "sim/AircraftState.hpp"
#include "sim/EngineState.hpp"
#include "sim/FDMState.hpp"
#include "sim/InitialCondition.hpp"
#include "sim/SimulationConfig.h"
#include "sim/control/ControlInput.hpp"
#include "sim/control/FlightControlMode.hpp"
#include "sim/execution/ExecutionRequest.hpp"
#include "sim/gnc/TrimTypes.hpp"
#include "sim/gnc/hold/Px4RollHoldParameterMetadata.hpp"
#include "sim/linearization/DynamicModeHistory.hpp"
#include "sim/linearization/LinearizationResult.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sim {
inline constexpr double MinimumAutomaticSimulationHz = 1.0;
inline constexpr double MaximumAutomaticSimulationHz = 1000.0;

enum class SimulationExecutionState {
  Running,
  Paused,
  Stopped,
};

enum class SimulationSlot {
  Primary,
  Baseline,
};

enum class SimulationCommandType {
  Start,
  Stop,
  Pause,
  Resume,
  Reset,
  TickOnce,
  RunExecution,
  SetAutomaticRate,
  SetMaximumSpeed,
  SetManualControl,
  SetControllerConfig,
};

struct PrimaryRollHoldConfig {
  bool enabled = false;
  double targetRollRad = 0.0;
  double rollAngleProportionalGain = 0.0;
  double rollRateProportionalGain = 0.0;
};

struct BaselineRollHoldConfig {
  bool enabled = false;
  double targetRollRad = 0.0;
  double timeConstantSec = gnc::GetPx4RollHoldParameterMetadata(
      gnc::Px4RollHoldParameter::TimeConstant)
                               .defaultValue;
  double maximumRollRateRadPerSec =
      math::DegToRad(gnc::GetPx4RollHoldParameterMetadata(
          gnc::Px4RollHoldParameter::MaximumRollRate)
              .defaultValue);
  double rateProportionalGain = gnc::GetPx4RollHoldParameterMetadata(
      gnc::Px4RollHoldParameter::RateProportionalGain)
                                    .defaultValue;
  double rateIntegralGain = gnc::GetPx4RollHoldParameterMetadata(
      gnc::Px4RollHoldParameter::RateIntegralGain)
                                .defaultValue;
  double rateDerivativeGain = gnc::GetPx4RollHoldParameterMetadata(
      gnc::Px4RollHoldParameter::RateDerivativeGain)
                                  .defaultValue;
  double rateFeedForwardGain = gnc::GetPx4RollHoldParameterMetadata(
      gnc::Px4RollHoldParameter::RateFeedForwardGain)
                                   .defaultValue;
  double integratorLimit = gnc::GetPx4RollHoldParameterMetadata(
      gnc::Px4RollHoldParameter::IntegratorLimit)
                               .defaultValue;
};

struct ControllerConfig {
  PrimaryRollHoldConfig primary;
  BaselineRollHoldConfig baseline;
};

struct SimulationCommand {
  SimulationCommandType type = SimulationCommandType::TickOnce;
  SimulationSlot slot = SimulationSlot::Primary;
  double value = 0.0;
  bool enabled = false;
  control::ControlInput manualControl;
  ControllerConfig controller;
};

struct BaselineRollHoldDiagnostics {
  double aileronCommand = 0.0;
  double bodyRateSetpointRadPerSec = 0.0;
  double rollErrorRad = 0.0;
  double airspeedScaling = 1.0;
};

struct AutopilotSnapshot {
  bool available = false;
  std::string strategyName;
  control::FlightControlMode mode = control::FlightControlMode::Manual;
  control::ControlInput manualControl;
  PrimaryRollHoldConfig primaryRollHold;
  BaselineRollHoldConfig baselineRollHold;
  BaselineRollHoldDiagnostics baselineDiagnostics;
};

struct TrimSnapshot {
  std::optional<gnc::TrimResult> result;
};

struct LinearizationSnapshot {
  bool available = false;
  bool automaticUpdatesEnabled = false;
  bool updateInProgress = false;
  std::string errorMessage;
  std::optional<gnc::LinearizationResult> result;
  gnc::DynamicModeHistory dynamicModeHistory;
};

struct ScenarioExecutionStatus {
  std::string name;
  double elapsedSec = 0.0;
  double durationSec = 0.0;
};

struct SimulationStatus {
  SimulationExecutionState executionState = SimulationExecutionState::Stopped;
  std::optional<ScenarioExecutionStatus> scenario;
  double automaticSimulationHz = DefaultSimulationHz;
  bool maximumSimulationSpeedEnabled = false;
  std::uint32_t pendingTickCount = 0;
  bool initialized = false;
  bool baselineAvailable = false;
  std::string lastError;
};

struct SimulationInstanceSnapshot {
  AircraftState aircraft;
  AircraftStateDerivative aircraftDerivative;
  FDMState fdmState;
  control::ControlInput controlInput;
  std::vector<EngineState> engines;
  InitialCondition currentCondition;
  double pitchTrim = 0.0;
  bool available = false;
};

struct SimulationSnapshot {
  SimulationStatus status;
  SimulationConfig config;
  std::optional<ResolvedExecutionSpec> appliedExecution;
  InitialCondition defaultInitialCondition;
  SimulationInstanceSnapshot primary;
  std::optional<SimulationInstanceSnapshot> baseline;
  AutopilotSnapshot primaryAutopilot;
  std::optional<AutopilotSnapshot> baselineAutopilot;
  TrimSnapshot trim;
  LinearizationSnapshot linearization;
  telemetry::recording::RecordingStatus telemetryRecording;
};

inline const char *ToString(SimulationExecutionState state) {
  switch (state) {
  case SimulationExecutionState::Running:
    return "Running";
  case SimulationExecutionState::Paused:
    return "Paused";
  case SimulationExecutionState::Stopped:
    return "Stopped";
  }

  return "Unknown";
}
} // namespace sim
