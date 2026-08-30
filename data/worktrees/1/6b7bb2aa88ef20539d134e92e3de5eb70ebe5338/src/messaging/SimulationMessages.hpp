#pragma once

#include "sim/runtime/SimulationContracts.hpp"
#include "sim/execution/ExecutionRequest.hpp"
#include "sim/telemetry/TelemetryContracts.hpp"
#include "contract/telemetry/RecordingTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace application::messaging {
using RequestId = std::uint64_t;

// Execution commands
struct SimulationStartCommand {};
struct SimulationStopCommand {};
struct SimulationPauseCommand {};
struct SimulationResumeCommand {};
struct SimulationStepCommand {};

struct SimulationRateCommand {
  double hz = sim::DefaultSimulationHz;
};

struct SimulationMaximumSpeedCommand {
  bool enabled = false;
};

// Request/response commands
struct SimulationResetCommand {
  RequestId requestId = 0;
  std::optional<sim::InitialCondition> initialCondition;
};

struct ManualControlCommand {
  RequestId requestId = 0;
  control::ControlInput input;
};

struct PrimaryRollHoldConfigCommand {
  RequestId requestId = 0;
  sim::PrimaryRollHoldConfig config;
};

struct BaselineRollHoldConfigCommand {
  RequestId requestId = 0;
  sim::BaselineRollHoldConfig config;
};

struct LinearizationConfigCommand {
  RequestId requestId = 0;
  bool automaticUpdatesEnabled = false;
};

struct TrimCommand {
  RequestId requestId = 0;
  gnc::TrimRequest request;
  bool fromCurrentState = false;
};

struct ExecutionRunCommand {
  RequestId requestId = 0;
  sim::ExecutionRequest request;
};

struct TelemetryRecordingCommand {
  RequestId requestId = 0;
  bool enabled = false;
};

// State events
struct SimulationStatusEvent {
  sim::SimulationStatus status;
};

struct SimulationSnapshotEvent {
  sim::SimulationSnapshot snapshot;
};

struct TelemetryFrameEvent {
  sim::SimulationSlot slot = sim::SimulationSlot::Primary;
  telemetry::TelemetryFrame frame;
};

struct ScenarioStatusEvent {
  std::optional<sim::ScenarioExecutionStatus> status;
};

struct TelemetryRecordingStatusEvent {
  telemetry::recording::RecordingStatus status;
};

// Operation result events
struct OperationResultEvent {
  RequestId requestId = 0;
  bool succeeded = false;
  std::string error;
};

struct SimulationResetResultEvent {
  RequestId requestId = 0;
  bool succeeded = false;
  std::string error;
};

struct TrimResultEvent {
  RequestId requestId = 0;
  bool succeeded = false;
  std::optional<gnc::TrimResult> result;
  std::string error;
};

struct ScenarioRunResultEvent {
  RequestId requestId = 0;
  bool succeeded = false;
  std::string error;
};

struct TelemetryRecordingResultEvent {
  RequestId requestId = 0;
  bool succeeded = false;
  telemetry::recording::RecordingStatus status;
  std::string error;
};
} // namespace application::messaging
