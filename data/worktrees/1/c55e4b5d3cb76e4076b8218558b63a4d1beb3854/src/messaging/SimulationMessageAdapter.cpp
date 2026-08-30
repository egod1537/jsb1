#include "messaging/SimulationMessageAdapter.hpp"

#include "messaging/SimulationMessages.hpp"
#include "sim/execution/ExecutionVariantResolver.hpp"
#include "sim/runtime/SimulationRuntime.hpp"

#include <utility>

namespace application::messaging {
SimulationMessageAdapter::SimulationMessageAdapter(MessageBus &bus,
    sim::SimulationRuntime &runtime)
    : bus_(bus), runtime_(runtime) {
  subscriptions_.push_back(
      bus_.Subscribe<SimulationStartCommand>([this](const auto &) {
        runtime_.Start();
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<SimulationStopCommand>([this](const auto &) {
        runtime_.Stop();
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<SimulationPauseCommand>([this](const auto &) {
        runtime_.Pause();
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<SimulationResumeCommand>([this](const auto &) {
        runtime_.Resume();
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<SimulationStepCommand>([this](const auto &) {
        runtime_.RequestTick();
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<SimulationRateCommand>([this](const auto &command) {
        runtime_.SetAutomaticSimulationHz(command.hz);
        PublishState();
      }));
  subscriptions_.push_back(bus_.Subscribe<SimulationMaximumSpeedCommand>(
      [this](const auto &command) {
        runtime_.SetMaximumSimulationSpeedEnabled(command.enabled);
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<SimulationResetCommand>([this](const auto &command) {
        const bool succeeded = command.initialCondition
                                   ? runtime_.Reset(*command.initialCondition)
                                   : runtime_.Reset();
        bus_.Publish(SimulationResetResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .error = succeeded ? std::string{}
                               : GetRuntimeError("Simulation reset failed."),
        });
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<ManualControlCommand>([this](const auto &command) {
        const bool succeeded = runtime_.SetManualControl(command.input);
        bus_.Publish(OperationResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .error = succeeded ? std::string{}
                               : GetRuntimeError("Manual control failed."),
        });
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<PrimaryRollHoldConfigCommand>([this](const auto &command) {
        const bool succeeded =
            runtime_.SetPrimaryRollHoldConfig(command.config);
        bus_.Publish(OperationResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .error = succeeded ? std::string{}
                               : GetRuntimeError(
                                     "Primary Roll Hold configuration failed."),
        });
        PublishState();
      }));
  subscriptions_.push_back(bus_.Subscribe<BaselineRollHoldConfigCommand>(
      [this](const auto &command) {
        const bool succeeded =
            runtime_.SetBaselineRollHoldConfig(command.config);
        bus_.Publish(OperationResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .error = succeeded
                         ? std::string{}
                         : GetRuntimeError(
                               "Baseline Roll Hold configuration failed."),
        });
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<LinearizationConfigCommand>([this](const auto &command) {
        const bool succeeded = runtime_.SetAutomaticLinearizationEnabled(
            command.automaticUpdatesEnabled);
        bus_.Publish(OperationResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .error = succeeded ? std::string{}
                               : GetRuntimeError(
                                     "Linearization configuration failed."),
        });
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<TrimCommand>([this](const auto &command) {
        const bool succeeded =
            runtime_.RunTrim(command.request, command.fromCurrentState);
        const sim::SimulationSnapshot snapshot = runtime_.GetSnapshot();
        bus_.Publish(TrimResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .result = snapshot.trim.result,
            .error = succeeded ? std::string{}
                               : GetRuntimeError("Trim request failed."),
        });
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<ExecutionRunCommand>([this](const auto &command) {
        sim::ResolvedExecutionSpec execution;
        std::string resolutionError;
        const bool resolved = sim::ExecutionVariantResolver::Resolve(
            command.request, execution, resolutionError);
        const bool succeeded = resolved && runtime_.RunExecution(execution);
        bus_.Publish(ScenarioRunResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .error = succeeded
                         ? std::string{}
                         : (resolved ? GetRuntimeError("Scenario start failed.")
                                     : std::move(resolutionError)),
        });
        PublishState();
      }));
  subscriptions_.push_back(
      bus_.Subscribe<TelemetryRecordingCommand>([this](const auto &command) {
        bool succeeded = true;
        if (command.enabled) {
          succeeded = runtime_.StartTelemetryRecording();
        } else {
          runtime_.StopTelemetryRecording();
        }
        const telemetry::recording::RecordingStatus status =
            runtime_.GetTelemetryRecordingStatus();
        bus_.Publish(TelemetryRecordingResultEvent{
            .requestId = command.requestId,
            .succeeded = succeeded,
            .status = status,
            .error = succeeded ? std::string{} : status.errorMessage,
        });
        PublishState();
      }));
}

void SimulationMessageAdapter::PublishState() {
  const sim::SimulationSnapshot snapshot = runtime_.GetSnapshot();
  bus_.Publish(SimulationStatusEvent{.status = snapshot.status});
  bus_.Publish(ScenarioStatusEvent{.status = snapshot.status.scenario});
  bus_.Publish(
      TelemetryRecordingStatusEvent{.status = snapshot.telemetryRecording});
  bus_.Publish(SimulationSnapshotEvent{.snapshot = snapshot});
  PublishTelemetry();
}

void SimulationMessageAdapter::PublishTelemetry() {
  const std::uint64_t primaryVersion =
      runtime_.GetTelemetryVersion(sim::SimulationSlot::Primary);
  if (primaryVersion != primaryTelemetryVersion_) {
    bus_.Publish(TelemetryFrameEvent{
        .slot = sim::SimulationSlot::Primary,
        .frame = runtime_.GetLatestTelemetryFrame(sim::SimulationSlot::Primary),
    });
    primaryTelemetryVersion_ = primaryVersion;
  }

  const std::uint64_t baselineVersion =
      runtime_.GetTelemetryVersion(sim::SimulationSlot::Baseline);
  if (baselineVersion != baselineTelemetryVersion_) {
    bus_.Publish(TelemetryFrameEvent{
        .slot = sim::SimulationSlot::Baseline,
        .frame =
            runtime_.GetLatestTelemetryFrame(sim::SimulationSlot::Baseline),
    });
    baselineTelemetryVersion_ = baselineVersion;
  }
}

std::string SimulationMessageAdapter::GetRuntimeError(
    std::string fallback) const {
  const std::string error = runtime_.GetStatus().lastError;
  return error.empty() ? std::move(fallback) : error;
}
} // namespace application::messaging
