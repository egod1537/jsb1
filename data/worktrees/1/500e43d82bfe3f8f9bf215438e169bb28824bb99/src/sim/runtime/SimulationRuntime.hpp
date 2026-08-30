#pragma once

#include "sim/runtime/SimulationContracts.hpp"
#include "sim/execution/ExecutionRequest.hpp"
#include "sim/telemetry/TelemetryContracts.hpp"
#include "contract/telemetry/RecordingTypes.hpp"
#include "sim/telemetry/recording/TelemetryRecordingService.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace sim {
class Simulation;
class ScenarioExecutor;

class SimulationRuntime {
public:
  SimulationRuntime(std::unique_ptr<Simulation> primarySimulation,
      std::unique_ptr<Simulation> baselineSimulation = nullptr);
  ~SimulationRuntime();

  SimulationRuntime(const SimulationRuntime &) = delete;
  SimulationRuntime &operator=(const SimulationRuntime &) = delete;

  static std::unique_ptr<SimulationRuntime> CreateForExecution(
      const ResolvedExecutionSpec &execution, std::string &error);

  // Lifetime and stepping
  bool Initialize(const SimulationConfig &config);
  void Shutdown();
  void Start();
  void Stop();
  void Pause();
  void Resume();
  bool Reset();
  bool Reset(const InitialCondition &initialCondition);
  void RequestTick();
  bool Tick();

  // Resolved execution
  bool RunExecution(const ResolvedExecutionSpec &execution);
  std::optional<ScenarioExecutionStatus> GetScenarioStatus() const;

  // Scheduling
  void SetAutomaticSimulationHz(double hz);
  double GetAutomaticSimulationHz() const;
  void SetMaximumSimulationSpeedEnabled(bool enabled);
  bool IsMaximumSimulationSpeedEnabled() const;

  // Data contracts
  SimulationStatus GetStatus() const;
  SimulationSnapshot GetSnapshot() const;
  std::uint64_t GetTelemetryVersion(SimulationSlot slot) const;
  telemetry::TelemetryFrame GetLatestTelemetryFrame(SimulationSlot slot) const;
  telemetry::TelemetrySnapshot GetTelemetrySnapshot(SimulationSlot slot) const;
  double GetSimulationTimeSec() const;
  std::optional<telemetry::recording::TelemetrySourceFrame>
  CaptureRecordingSource() const;
  std::vector<telemetry::recording::ScenarioEvent> TakeScenarioEvents();

  // External command boundary
  bool SetManualControl(const control::ControlInput &input);
  bool SetPrimaryRollHoldConfig(const PrimaryRollHoldConfig &config);
  bool SetBaselineRollHoldConfig(const BaselineRollHoldConfig &config);
  bool RunTrim(const gnc::TrimRequest &request, bool fromCurrentState);
  bool SetAutomaticLinearizationEnabled(bool enabled);

  // Telemetry recording
  bool StartTelemetryRecording();
  bool StartTelemetryRecording(const std::filesystem::path &path,
      const telemetry::recording::RecordingMetadata &metadata);
  void StopTelemetryRecording();
  telemetry::recording::RecordingStatus GetTelemetryRecordingStatus() const;

private:
  // Simulation coordination
  bool ResetSimulations(const InitialCondition *initialCondition);
  bool SynchronizeBaselineControlState();
  void FinishScenario();
  void RecordPendingScenarioCommandEvent();
  bool SelectExecutionVariant(ExecutionVariant variant);
  bool ReinitializeForScenario(const SimulationScenario &scenario);
  void RestoreInteractiveSimulationOrder();
  SimulationInstanceSnapshot CaptureSnapshot(
      const Simulation &simulation) const;
  AutopilotSnapshot CaptureAutopilotSnapshot(
      const Simulation &simulation) const;
  LinearizationSnapshot CaptureLinearizationSnapshot() const;
  Simulation *GetSimulation(SimulationSlot slot);
  const Simulation *GetSimulation(SimulationSlot slot) const;

  // Owned simulations and services
  std::unique_ptr<Simulation> primarySimulation_;
  std::unique_ptr<Simulation> baselineSimulation_;
  std::unique_ptr<ScenarioExecutor> scenarioExecutor_;
  std::optional<ResolvedExecutionSpec> resolvedExecution_;
  bool scenarioSimulationSwapped_ = false;
  telemetry::recording::TelemetryRecordingService telemetryRecording_;
  std::vector<telemetry::recording::ScenarioEvent> pendingScenarioEvents_;

  // Configuration and execution state
  SimulationConfig config_;
  SimulationExecutionState executionState_ = SimulationExecutionState::Stopped;
  double automaticSimulationHz_ = DefaultSimulationHz;
  bool maximumSimulationSpeedEnabled_ = false;
  std::uint32_t pendingTicks_ = 0;
  bool initialized_ = false;
  std::string lastError_;
};
} // namespace sim
