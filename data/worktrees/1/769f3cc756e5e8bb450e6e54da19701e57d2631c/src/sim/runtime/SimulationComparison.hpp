#pragma once

#include "contract/telemetry/RecordingTypes.hpp"
#include "sim/execution/ExecutionRequest.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sim {
class SimulationRuntime;

enum class ComparisonExecutionState {
  Running,
  Completed,
  Stopped,
  Failed,
};

struct ComparisonVariantResult {
  ComparisonExecutionState state = ComparisonExecutionState::Stopped;
  std::string error;
};

struct ComparisonObservation {
  telemetry::recording::TelemetryFrame telemetry;
  std::vector<telemetry::recording::ScenarioEvent> scenarioEvents;
};

class SimulationComparison {
public:
  using RuntimeFactory = std::function<std::unique_ptr<SimulationRuntime>(
      const ResolvedExecutionSpec &, std::string &)>;

  ~SimulationComparison();

  SimulationComparison(const SimulationComparison &) = delete;
  SimulationComparison &operator=(const SimulationComparison &) = delete;

  static std::unique_ptr<SimulationComparison> Create(
      const SimulationScenario &scenario, const ScenarioSource &source,
      std::string &error, RuntimeFactory runtimeFactory = {});

  // Synchronized execution lifecycle
  bool Tick();
  void Stop();
  void Shutdown();

  // Comparison state and output
  bool IsRunning() const;
  bool IsFinished() const;
  ComparisonExecutionState GetState() const;
  double GetSimulationTimeSec() const;
  std::uint64_t GetStepCount() const;
  const std::string &GetLastError() const;
  const ComparisonVariantResult &GetVariantResult(
      ExecutionVariant variant) const;
  ComparisonObservation TakeObservation();

private:
  SimulationComparison(std::unique_ptr<SimulationRuntime> baseline,
      std::unique_ptr<SimulationRuntime> primary, double dtSec,
      double durationSec);

  bool CollectEvents();
  bool ValidateClock() const;
  bool Fail(ExecutionVariant variant, std::string error);

  // Independent variant runtimes
  std::unique_ptr<SimulationRuntime> baselineRuntime_;
  std::unique_ptr<SimulationRuntime> primaryRuntime_;

  // Shared execution plan and clock
  double dtSec_ = 0.0;
  double durationSec_ = 0.0;
  std::uint64_t stepCount_ = 0;
  ComparisonExecutionState state_ = ComparisonExecutionState::Running;

  // Aggregated results
  ComparisonVariantResult baselineResult_{ComparisonExecutionState::Running};
  ComparisonVariantResult primaryResult_{ComparisonExecutionState::Running};
  ComparisonObservation observation_;
  std::string lastError_;
};
} // namespace sim
