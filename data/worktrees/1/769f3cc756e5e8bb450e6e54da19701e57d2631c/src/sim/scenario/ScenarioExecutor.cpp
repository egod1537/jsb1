#include "sim/scenario/ScenarioExecutor.hpp"

#include "sim/Simulation.hpp"
#include "sim/control/FlightControlManager.hpp"
#include "sim/gnc/autopilot/IRollHoldAutopilot.hpp"
#include "common/math/Math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace sim {
namespace {
FDMEnvironmentState MakeEnvironment(const Simulation &reference,
    bool windEnabled) {
  FDMEnvironmentState environment =
      reference.GetAircraft()
          .ExtractFDMState(FDMStateFlags::Environment)
          .environment;
  if (!windEnabled) {
    environment.windNedFps.fill(0.0);
    environment.gustNedFps.fill(0.0);
    environment.turbulenceNedFps.fill(0.0);
    environment.turbulenceType = 0;
    environment.turbulenceGain = 0.0;
    environment.turbulenceRate = 0.0;
    environment.turbulenceRhythmicity = 0.0;
    environment.windSpeedAt20FtFps = 0.0;
  }
  return environment;
}

gnc::IRollHoldAutopilot *FindRollHold(Simulation &simulation) {
  auto *manager = simulation.GetComponent<control::FlightControlManager>();
  return manager != nullptr
             ? dynamic_cast<gnc::IRollHoldAutopilot *>(&manager->GetAutopilot())
             : nullptr;
}

bool ConfigureRollHold(Simulation &simulation, double targetRollRad,
    bool enabled) {
  auto *manager = simulation.GetComponent<control::FlightControlManager>();
  auto *rollHold = FindRollHold(simulation);
  if (manager == nullptr || rollHold == nullptr) {
    return false;
  }
  rollHold->SetTargetRollRad(targetRollRad);
  rollHold->SetRollHoldEnabled(enabled);
  manager->SetMode(enabled ? control::FlightControlMode::Autopilot
                           : control::FlightControlMode::Manual);
  return true;
}
} // namespace

ScenarioExecutor::ScenarioExecutor(Simulation &simulation)
    : simulation_(simulation) {}

bool ScenarioExecutor::Start(const SimulationScenario &scenario, double dtSec) {
  if (state_ == ScenarioExecutorState::Running) {
    return Fail("scenario executor is already running");
  }
  lastError_.clear();
  pendingCommandActivations_.clear();
  if (!simulation_.IsInitialized()) {
    return Fail("scenario simulation must be initialized");
  }
  std::string validationError;
  if (!ValidateSimulationScenario(scenario, &validationError)) {
    return Fail(validationError);
  }
  const auto targetSteps = CalculateStepCount(scenario.durationSec, dtSec);
  if (!targetSteps) {
    return Fail("scenario duration and timestep produce an invalid step count");
  }
  if (std::abs(simulation_.GetTickSizeSec() - dtSec) > 1.0e-12) {
    return Fail(
        "simulation configuration timestep does not match executor timestep");
  }
  if (FindRollHold(simulation_) == nullptr) {
    return Fail("scenario simulation does not support Roll Hold");
  }

  scenario_ = scenario;
  dtSec_ = dtSec;
  targetStepCount_ = *targetSteps;
  eventStepIndices_.clear();
  eventStepIndices_.reserve(scenario.events.size());
  for (const ScenarioEventDefinition &event : scenario.events) {
    eventStepIndices_.push_back(
        static_cast<std::uint64_t>(std::llround(event.timeSec / dtSec)));
  }
  stepCount_ = 0;
  nextEventIndex_ = 0;
  commandActive_ = false;
  targetRollRad_ = 0.0;
  if (!ResetSimulations()) {
    return Fail("failed to reset scenario simulation");
  }
  state_ = ScenarioExecutorState::Running;
  if (!ApplyControlState()) {
    return Fail("failed to apply initial scenario control state");
  }
  return true;
}

ScenarioStepResult ScenarioExecutor::Step() {
  ScenarioStepResult result;
  if (state_ != ScenarioExecutorState::Running) {
    lastError_ = "scenario executor is not running";
    return result;
  }
  if (!ApplyControlState()) {
    Fail("failed to apply scenario control state");
    return result;
  }
  if (!simulation_.Step(dtSec_)) {
    Fail("scenario simulation step failed");
    return result;
  }

  ++stepCount_;
  result.succeeded = true;
  if (stepCount_ >= targetStepCount_) {
    DisableRollHold();
    state_ = ScenarioExecutorState::Completed;
    result.completed = true;
  }
  return result;
}

void ScenarioExecutor::Stop() {
  if (state_ == ScenarioExecutorState::Running) {
    DisableRollHold();
    state_ = ScenarioExecutorState::Stopped;
  }
}

ScenarioExecutorState ScenarioExecutor::GetState() const { return state_; }

bool ScenarioExecutor::IsRunning() const {
  return state_ == ScenarioExecutorState::Running;
}

bool ScenarioExecutor::IsFinished() const {
  return state_ == ScenarioExecutorState::Completed;
}

double ScenarioExecutor::GetElapsedSec() const {
  return std::min(scenario_.durationSec,
      static_cast<double>(stepCount_) * dtSec_);
}

double ScenarioExecutor::GetStepSizeSec() const { return dtSec_; }

std::uint64_t ScenarioExecutor::GetStepCount() const { return stepCount_; }

std::uint64_t ScenarioExecutor::GetTargetStepCount() const {
  return targetStepCount_;
}

const SimulationScenario *ScenarioExecutor::GetScenario() const {
  return state_ == ScenarioExecutorState::Idle ? nullptr : &scenario_;
}

const std::string &ScenarioExecutor::GetLastError() const { return lastError_; }

std::vector<ScenarioCommandActivation>
ScenarioExecutor::TakeCommandActivations() {
  return std::exchange(pendingCommandActivations_, {});
}

std::optional<std::uint64_t> ScenarioExecutor::CalculateStepCount(
    double durationSec, double dtSec) {
  if (!std::isfinite(durationSec) || durationSec <= 0.0 || !std::isfinite(dtSec)
      || dtSec <= 0.0) {
    return std::nullopt;
  }
  const double ratio = durationSec / dtSec;
  if (!std::isfinite(ratio)
      || ratio
             > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return std::nullopt;
  }
  const double nearest = std::round(ratio);
  const double tolerance = std::numeric_limits<double>::epsilon()
                           * std::max(1.0, std::abs(ratio)) * 8.0;
  const double steps =
      std::abs(ratio - nearest) <= tolerance ? nearest : std::ceil(ratio);
  if (steps < 1.0) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(steps);
}

bool ScenarioExecutor::ResetSimulations() {
  const FDMEnvironmentState environment =
      MakeEnvironment(simulation_, scenario_.windEnabled);
  const SimulationResetOptions options{
      .runTrim = scenario_.runTrim,
      .trimMode = scenario_.trimMode,
      .environment = environment,
  };
  return simulation_.Reset(scenario_.initialCondition, options);
}

bool ScenarioExecutor::ApplyControlState() {
  while (nextEventIndex_ < scenario_.events.size()
         && stepCount_ >= eventStepIndices_[nextEventIndex_]) {
    targetRollRad_ =
        math::DegToRad(scenario_.events[nextEventIndex_].command.rollDeg);
    commandActive_ = true;
    pendingCommandActivations_.push_back({
        .simulationTimeSec = simulation_.GetTime(),
        .targetRollRad = targetRollRad_,
    });
    ++nextEventIndex_;
  }
  if (!ConfigureRollHold(simulation_, targetRollRad_, commandActive_)) {
    return false;
  }
  return true;
}

void ScenarioExecutor::DisableRollHold() {
  ConfigureRollHold(simulation_, targetRollRad_, false);
}

bool ScenarioExecutor::Fail(std::string message) {
  if (state_ == ScenarioExecutorState::Running) {
    DisableRollHold();
  }
  state_ = ScenarioExecutorState::Failed;
  lastError_ = std::move(message);
  return false;
}
} // namespace sim
