#pragma once

#include "messaging/MessageBus.hpp"
#include "messaging/SimulationMessages.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace application {
inline constexpr double MinimumAutomaticSimulationHz =
    sim::MinimumAutomaticSimulationHz;
inline constexpr double MaximumAutomaticSimulationHz =
    sim::MaximumAutomaticSimulationHz;

using SimulationExecutionState = sim::SimulationExecutionState;
using ScenarioExecutionStatus = sim::ScenarioExecutionStatus;

inline const char *ToString(SimulationExecutionState state) {
  return sim::ToString(state);
}

class SimulationMessageClient final {
public:
  explicit SimulationMessageClient(messaging::MessageBus &bus);

  // Execution and scenarios
  bool RunExecution(const sim::ExecutionRequest &request);
  std::optional<ScenarioExecutionStatus> GetScenarioExecutionStatus() const;
  SimulationExecutionState GetSimulationExecutionState() const;
  void StartSimulation();
  void StopSimulation();
  void PauseSimulation();
  void ResumeSimulation();
  void RequestSimulationTick();
  bool ResetSimulation();
  bool ResetSimulation(const sim::InitialCondition &initialCondition);

  // Scheduling
  double GetAutomaticSimulationHz() const;
  void SetAutomaticSimulationHz(double hz);
  bool IsMaximumSimulationSpeedEnabled() const;
  void SetMaximumSimulationSpeedEnabled(bool enabled);
  std::uint32_t GetPendingSimulationTickCount() const;

  // Cached state and command publication
  sim::SimulationSnapshot GetSimulationSnapshot() const;
  std::shared_ptr<const telemetry::TelemetrySnapshot> GetTelemetrySnapshot(
      sim::SimulationSlot slot) const;
  bool SetManualControl(const control::ControlInput &input);
  bool SetPrimaryRollHoldConfig(const sim::PrimaryRollHoldConfig &config);
  bool SetBaselineRollHoldConfig(const sim::BaselineRollHoldConfig &config);
  bool RunTrim(const gnc::TrimRequest &request, bool fromCurrentState);
  bool SetAutomaticLinearizationEnabled(bool enabled);
  std::optional<std::string> GetLastCommandError() const;

  // Telemetry recording
  bool StartTelemetryRecording();
  void StopTelemetryRecording();
  telemetry::recording::RecordingStatus GetTelemetryRecordingStatus() const;
  bool OpenTelemetryRecordingsFolder() const;

private:
  struct RequestResult {
    bool succeeded = false;
    std::string error;
  };

  // Request/result correlation
  messaging::RequestId NextRequestId();
  bool TakeRequestResult(messaging::RequestId requestId);

  // Event cache updates
  void ReceiveTelemetry(const messaging::TelemetryFrameEvent &event);

  // Dependencies
  messaging::MessageBus &bus_;

  // Cached event data
  mutable std::mutex cacheMutex_;
  sim::SimulationSnapshot latestSnapshot_;
  sim::SimulationStatus latestStatus_;
  telemetry::TelemetrySnapshot primaryTelemetryCache_;
  telemetry::TelemetrySnapshot baselineTelemetryCache_;
  mutable std::shared_ptr<const telemetry::TelemetrySnapshot> primaryTelemetry_;
  mutable std::shared_ptr<const telemetry::TelemetrySnapshot>
      baselineTelemetry_;
  telemetry::recording::RecordingStatus recordingStatus_;
  std::unordered_set<messaging::RequestId> pendingRequests_;
  std::unordered_map<messaging::RequestId, RequestResult> requestResults_;
  std::optional<std::string> lastCommandError_;

  // Declared last so subscriptions are destroyed before callback state.
  std::vector<messaging::Subscription> subscriptions_;
};
} // namespace application
