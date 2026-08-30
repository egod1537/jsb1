#pragma once

#include "SimulationRunner.hpp"
#include "sim/telemetry/recording/TelemetryRecordingService.hpp"

namespace runner {
class McapRunObserver final : public ISimulationRunObserver {
public:
  bool OnRunStarted(const SimulationRunInfo &info,
      const SimulationRunObservation &observation, std::string &error) override;
  bool OnSimulationStep(const SimulationRunInfo &info,
      const SimulationRunObservation &observation, std::string &error) override;
  bool OnRunFinished(const SimulationRunInfo &info, const RunnerResult &result,
      std::string &error) override;

private:
  bool Consume(const SimulationRunObservation &observation, std::string &error);

  telemetry::recording::TelemetryRecordingService recording_;
  bool started_ = false;
};
} // namespace runner
