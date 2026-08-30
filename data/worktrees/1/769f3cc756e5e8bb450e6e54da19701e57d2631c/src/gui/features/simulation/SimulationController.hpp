#pragma once

#include "gui/features/simulation/SimulationEvents.hpp"
#include "gui/features/simulation/SimulationModel.hpp"
#include "sim/runtime/SimulationContracts.hpp"
#include "contract/telemetry/RecordingTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace application {
class SimulationMessageClient;
}

namespace gui {
struct SimulationTransportProps {
  sim::SimulationExecutionState executionState =
      sim::SimulationExecutionState::Stopped;
  std::optional<sim::ScenarioExecutionStatus> scenarioStatus;
  telemetry::recording::RecordingStatus recordingStatus;
  double automaticHz = 0.0;
  std::uint32_t pendingTickCount = 0;
  bool maximumSpeed = false;
};

class SimulationController {
public:
  explicit SimulationController(application::SimulationMessageClient &client);

  // Immutable state for views
  SimulationTransportProps GetTransportProps() const;
  const InitialConditionModel &GetInitialConditionModel() const {
    return initialCondition_;
  }
  void Synchronize(const sim::SimulationSnapshot &snapshot);

  // Transport events
  void Handle(const SimulationStartRequested &event);
  void Handle(const SimulationStopRequested &event);
  void Handle(const SimulationPlaybackToggled &event);
  void Handle(const SimulationPauseRequested &event);
  void Handle(const SimulationResumeRequested &event);
  void Handle(const SimulationResetRequested &event);
  void Handle(const SimulationStepRequested &event);
  void Handle(const SimulationRateChanged &event);
  void Handle(const MaximumSimulationSpeedChanged &event);
  void Handle(const TelemetryRecordingToggled &event);
  void Handle(const OpenTelemetryFolderRequested &event);
  bool Handle(const ScenarioLaunchRequested &event);
  std::optional<std::string> GetLastCommandError() const;

  // Initial-condition child events
  void Handle(const InitialConditionFieldChanged &event);
  void Handle(const UseCurrentInitialConditionRequested &event);
  void Handle(const RestoreDefaultInitialConditionRequested &event);
  void Handle(const ResetWithInitialConditionRequested &event);

private:
  void ResetSimulation(const sim::InitialCondition *initialCondition);

  // Dependencies
  application::SimulationMessageClient &client_;

  // Child feature state
  InitialConditionModel initialCondition_;
};
} // namespace gui
