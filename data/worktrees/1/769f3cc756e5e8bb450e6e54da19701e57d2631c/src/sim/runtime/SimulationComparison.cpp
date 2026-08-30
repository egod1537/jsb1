#include "sim/runtime/SimulationComparison.hpp"

#include "sim/execution/ExecutionVariantResolver.hpp"
#include "sim/runtime/SimulationRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sim {
namespace {
constexpr double ClockToleranceSec = 1.0e-9;

bool SameEvent(const telemetry::recording::ScenarioEvent &left,
    const telemetry::recording::ScenarioEvent &right) {
  if (left.type != right.type
      || std::abs(left.simulationTimeSec - right.simulationTimeSec)
             > ClockToleranceSec
      || left.targetRollRad.has_value() != right.targetRollRad.has_value()) {
    return false;
  }
  return !left.targetRollRad
         || std::abs(*left.targetRollRad - *right.targetRollRad)
                <= ClockToleranceSec;
}

ResolvedExecutionSpec Resolve(const SimulationScenario &scenario,
    const ScenarioSource &source, ExecutionVariant variant,
    std::string &error) {
  ResolvedExecutionSpec resolved;
  error.clear();
  if (!ExecutionVariantResolver::Resolve({scenario, variant, source},
          resolved,
          error)
      && error.empty()) {
    error = "failed to resolve execution variant";
  }
  return resolved;
}
} // namespace

SimulationComparison::SimulationComparison(
    std::unique_ptr<SimulationRuntime> baseline,
    std::unique_ptr<SimulationRuntime> primary, double dtSec,
    double durationSec)
    : baselineRuntime_(std::move(baseline)),
      primaryRuntime_(std::move(primary)), dtSec_(dtSec),
      durationSec_(durationSec) {}

SimulationComparison::~SimulationComparison() { Shutdown(); }

std::unique_ptr<SimulationComparison> SimulationComparison::Create(
    const SimulationScenario &scenario, const ScenarioSource &source,
    std::string &error, RuntimeFactory runtimeFactory) {
  ResolvedExecutionSpec baselineSpec =
      Resolve(scenario, source, ExecutionVariant::Baseline, error);
  if (!error.empty()) {
    return nullptr;
  }
  ResolvedExecutionSpec primarySpec =
      Resolve(scenario, source, ExecutionVariant::Primary, error);
  if (!error.empty()) {
    return nullptr;
  }

  if (!runtimeFactory) {
    runtimeFactory = [](const ResolvedExecutionSpec &execution,
                         std::string &factoryError) {
      return SimulationRuntime::CreateForExecution(execution, factoryError);
    };
  }
  auto baseline = runtimeFactory(baselineSpec, error);
  if (baseline == nullptr) {
    error = "baseline initialization failed: " + error;
    return nullptr;
  }
  auto primary = runtimeFactory(primarySpec, error);
  if (primary == nullptr) {
    baseline->Shutdown();
    error = "primary initialization failed: " + error;
    return nullptr;
  }
  if (!baseline->RunExecution(baselineSpec)) {
    error =
        "baseline initialization failed: " + baseline->GetStatus().lastError;
    primary->Shutdown();
    baseline->Shutdown();
    return nullptr;
  }
  if (!primary->RunExecution(primarySpec)) {
    error = "primary initialization failed: " + primary->GetStatus().lastError;
    baseline->Stop();
    primary->Shutdown();
    baseline->Shutdown();
    return nullptr;
  }

  auto comparison = std::unique_ptr<SimulationComparison>(
      new SimulationComparison(std::move(baseline),
          std::move(primary),
          scenario.dtSec,
          scenario.durationSec));
  if (!comparison->CollectEvents()) {
    error = comparison->GetLastError();
    comparison->Shutdown();
    return nullptr;
  }
  error.clear();
  return comparison;
}

bool SimulationComparison::Tick() {
  if (state_ != ComparisonExecutionState::Running) {
    lastError_ = "comparison execution is not running";
    return false;
  }
  observation_.telemetry = {};
  observation_.scenarioEvents.clear();

  if (!baselineRuntime_->Tick()) {
    return Fail(ExecutionVariant::Baseline,
        baselineRuntime_->GetStatus().lastError);
  }
  if (!primaryRuntime_->Tick()) {
    return Fail(ExecutionVariant::Primary,
        primaryRuntime_->GetStatus().lastError);
  }
  ++stepCount_;
  if (!ValidateClock()) {
    return Fail(ExecutionVariant::Primary,
        "variant simulation clocks diverged from the shared step clock");
  }
  if (!CollectEvents()) {
    return false;
  }

  observation_.telemetry.simulationTimeSec = GetSimulationTimeSec();
  observation_.telemetry.baseline = baselineRuntime_->CaptureRecordingSource();
  observation_.telemetry.primary = primaryRuntime_->CaptureRecordingSource();

  const bool baselineActive = baselineRuntime_->GetScenarioStatus().has_value();
  const bool primaryActive = primaryRuntime_->GetScenarioStatus().has_value();
  if (baselineActive != primaryActive) {
    return Fail(ExecutionVariant::Primary,
        "variant runtimes completed on different simulation steps");
  }
  if (!baselineActive) {
    state_ = ComparisonExecutionState::Completed;
    baselineResult_.state = ComparisonExecutionState::Completed;
    primaryResult_.state = ComparisonExecutionState::Completed;
  }
  lastError_.clear();
  return true;
}

void SimulationComparison::Stop() {
  if (state_ != ComparisonExecutionState::Running) {
    return;
  }
  baselineRuntime_->Stop();
  primaryRuntime_->Stop();
  if (!CollectEvents()) {
    return;
  }
  state_ = ComparisonExecutionState::Stopped;
  baselineResult_.state = ComparisonExecutionState::Stopped;
  primaryResult_.state = ComparisonExecutionState::Stopped;
}

void SimulationComparison::Shutdown() {
  if (baselineRuntime_) {
    baselineRuntime_->Shutdown();
  }
  if (primaryRuntime_) {
    primaryRuntime_->Shutdown();
  }
}

bool SimulationComparison::IsRunning() const {
  return state_ == ComparisonExecutionState::Running;
}

bool SimulationComparison::IsFinished() const {
  return state_ == ComparisonExecutionState::Completed;
}

ComparisonExecutionState SimulationComparison::GetState() const {
  return state_;
}

double SimulationComparison::GetSimulationTimeSec() const {
  return std::min(durationSec_, static_cast<double>(stepCount_) * dtSec_);
}

std::uint64_t SimulationComparison::GetStepCount() const { return stepCount_; }

const std::string &SimulationComparison::GetLastError() const {
  return lastError_;
}

const ComparisonVariantResult &SimulationComparison::GetVariantResult(
    ExecutionVariant variant) const {
  return variant == ExecutionVariant::Baseline ? baselineResult_
                                               : primaryResult_;
}

ComparisonObservation SimulationComparison::TakeObservation() {
  return std::exchange(observation_, {});
}

bool SimulationComparison::CollectEvents() {
  auto baselineEvents = baselineRuntime_->TakeScenarioEvents();
  auto primaryEvents = primaryRuntime_->TakeScenarioEvents();
  if (baselineEvents.size() != primaryEvents.size()) {
    return Fail(ExecutionVariant::Primary,
        "variant scenario event schedules diverged");
  }
  for (std::size_t index = 0; index < baselineEvents.size(); ++index) {
    if (!SameEvent(baselineEvents[index], primaryEvents[index])) {
      return Fail(ExecutionVariant::Primary,
          "variant scenario events diverged at the shared step");
    }
  }
  observation_.scenarioEvents.insert(observation_.scenarioEvents.end(),
      primaryEvents.begin(),
      primaryEvents.end());
  return true;
}

bool SimulationComparison::ValidateClock() const {
  const double expected = GetSimulationTimeSec();
  return std::abs(baselineRuntime_->GetSimulationTimeSec() - expected)
             <= ClockToleranceSec
         && std::abs(primaryRuntime_->GetSimulationTimeSec() - expected)
                <= ClockToleranceSec;
}

bool SimulationComparison::Fail(ExecutionVariant variant, std::string error) {
  lastError_ = std::string(ToString(variant)) + ": " + error;
  state_ = ComparisonExecutionState::Failed;
  ComparisonVariantResult &failed =
      variant == ExecutionVariant::Baseline ? baselineResult_ : primaryResult_;
  ComparisonVariantResult &peer =
      variant == ExecutionVariant::Baseline ? primaryResult_ : baselineResult_;
  failed.state = ComparisonExecutionState::Failed;
  failed.error = std::move(error);
  peer.state = ComparisonExecutionState::Stopped;
  baselineRuntime_->Stop();
  primaryRuntime_->Stop();
  return false;
}
} // namespace sim
