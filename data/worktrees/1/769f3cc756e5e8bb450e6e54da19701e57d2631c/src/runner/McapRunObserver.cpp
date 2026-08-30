#include "McapRunObserver.hpp"

#include "sim/execution/ExecutionVariant.hpp"
#include "contract/telemetry/RecordingTypes.hpp"

#include <string>

namespace runner {
namespace {
std::string RecordingError(const telemetry::recording::RecordingStatus &status,
    std::string_view context) {
  std::string error(context);
  if (!status.errorMessage.empty()) {
    error += ": ";
    error += status.errorMessage;
  }
  return error;
}
} // namespace

bool McapRunObserver::OnRunStarted(const SimulationRunInfo &info,
    const SimulationRunObservation &observation, std::string &error) {
  telemetry::recording::RecordingMetadata metadata;
  metadata.contractVersion = JSB_CONTRACT_VERSION;
  metadata.telemetrySchemaVersion = JSB_TELEMETRY_SCHEMA_VERSION;
  metadata.applicationVersion = JSB_APPLICATION_VERSION;
  metadata.gitCommit = JSB_GIT_COMMIT;
  metadata.runtimeBranch = JSB_RUNTIME_BRANCH;
  metadata.aircraft = info.aircraft;
  metadata.scenarioName = info.scenarioName;
  metadata.scenarioFile = info.scenarioFile;
  metadata.scenarioDigest = info.scenarioDigest;
  metadata.scenarioSchemaVersion = info.scenarioSchemaVersion;
  metadata.scenarioType = info.scenarioType;
  metadata.scenarioDurationSec = info.durationSec;
  metadata.simulationDtSec = info.dtSec;
  metadata.executionMode = std::string(ToString(info.mode));
  if (info.mode == ExecutionMode::Compare) {
    metadata.executionVariants = "baseline,primary";
    metadata.primaryAutopilot = "primary";
    metadata.baselineAutopilot = "baseline";
  } else if (info.variant) {
    metadata.executionVariant = std::string(sim::ToString(*info.variant));
    metadata.primaryAutopilot = metadata.executionVariant;
  }

  telemetry::recording::TelemetryRecordingConfig config;
  config.outputPath = info.outputDirectory / "telemetry.mcap";
  config.recordPrimary = true;
  config.recordBaseline = info.mode == ExecutionMode::Compare;
  if (!recording_.Start(config, metadata)) {
    error = RecordingError(recording_.GetStatus(),
        "failed to initialize MCAP recorder for telemetry.mcap");
    return false;
  }
  started_ = true;
  return Consume(observation, error);
}

bool McapRunObserver::OnSimulationStep(const SimulationRunInfo &,
    const SimulationRunObservation &observation, std::string &error) {
  return Consume(observation, error);
}

bool McapRunObserver::Consume(const SimulationRunObservation &observation,
    std::string &error) {
  if (!started_) {
    error = "MCAP recorder was not started";
    return false;
  }
  recording_.Consume(observation.telemetry);
  for (const auto &event : observation.scenarioEvents) {
    recording_.RecordScenarioEvent(event);
  }
  const telemetry::recording::RecordingStatus status = recording_.GetStatus();
  if (status.state != telemetry::recording::RecordingState::Recording) {
    error = RecordingError(status, "failed to record telemetry.mcap");
    return false;
  }
  return true;
}

bool McapRunObserver::OnRunFinished(const SimulationRunInfo &,
    const RunnerResult &, std::string &error) {
  if (!started_) {
    return true;
  }
  recording_.Stop();
  started_ = false;
  const telemetry::recording::RecordingStatus status = recording_.GetStatus();
  if (status.state != telemetry::recording::RecordingState::Idle) {
    error = RecordingError(status, "failed to finalize telemetry.mcap");
    return false;
  }
  return true;
}
} // namespace runner
