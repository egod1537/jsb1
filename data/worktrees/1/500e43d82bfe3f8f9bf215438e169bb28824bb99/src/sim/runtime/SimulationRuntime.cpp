#include "sim/runtime/SimulationRuntime.hpp"

#include "sim/Aircraft.hpp"
#include "sim/Simulation.hpp"
#include "sim/control/FlightControlManager.hpp"
#include "sim/execution/ExecutionVariantResolver.hpp"
#include "sim/gnc/autopilot/IAutopilotAnalysis.hpp"
#include "sim/gnc/autopilot/MyAutopilot.hpp"
#include "sim/gnc/autopilot/PX4Autopilot.hpp"
#include "sim/scenario/ScenarioExecutor.hpp"
#include "sim/scenario/SimulationScenario.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

namespace sim {
namespace {
double ClampAutomaticSimulationHz(double hz) {
  if (!std::isfinite(hz)) {
    return MinimumAutomaticSimulationHz;
  }

  return std::clamp(hz,
      MinimumAutomaticSimulationHz,
      MaximumAutomaticSimulationHz);
}

std::string GetSimulationError(const Simulation *simulation,
    std::string fallback) {
  if (simulation == nullptr) {
    return fallback;
  }
  return simulation->GetErrorTracker().GetLastError().value_or(
      std::move(fallback));
}
} // namespace

SimulationRuntime::SimulationRuntime(
    std::unique_ptr<Simulation> primarySimulation,
    std::unique_ptr<Simulation> baselineSimulation)
    : primarySimulation_(std::move(primarySimulation)),
      baselineSimulation_(std::move(baselineSimulation)) {}

SimulationRuntime::~SimulationRuntime() { Shutdown(); }

std::unique_ptr<SimulationRuntime> SimulationRuntime::CreateForExecution(
    const ResolvedExecutionSpec &execution, std::string &error) {
  const SimulationScenario &scenario = execution.scenario;
  ScenarioValidationError validationError;
  if (!ValidateSimulationScenario(scenario, &validationError)) {
    error = validationError.ToString();
    return nullptr;
  }
  std::unique_ptr<gnc::IAutopilot> autopilot =
      ExecutionVariantResolver::CreateAutopilot(execution.variant);
  if (autopilot == nullptr) {
    error = "failed to construct execution variant '"
            + std::string(ToString(execution.variant)) + "'";
    return nullptr;
  }
  auto runtime = std::make_unique<SimulationRuntime>(
      std::make_unique<Simulation>(std::move(autopilot)));
  SimulationConfig config;
  config.aircraftName = scenario.aircraft;
  config.simulationHz = 1.0 / scenario.dtSec;
  if (!runtime->Initialize(config)) {
    error = runtime->GetStatus().lastError;
    return nullptr;
  }
  error.clear();
  return runtime;
}

bool SimulationRuntime::Initialize(const SimulationConfig &config) {
  if (initialized_) {
    return true;
  }
  if (primarySimulation_ == nullptr) {
    lastError_ = "Simulation runtime requires a primary simulation.";
    return false;
  }

  config_ = config;
  automaticSimulationHz_ = ClampAutomaticSimulationHz(config.simulationHz);
  if (!primarySimulation_->Initialize(config_)) {
    lastError_ = GetSimulationError(primarySimulation_.get(),
        "Failed to initialize primary simulation.");
    return false;
  }
  if (baselineSimulation_ != nullptr
      && !baselineSimulation_->Initialize(config_)) {
    lastError_ = GetSimulationError(baselineSimulation_.get(),
        "Failed to initialize baseline simulation.");
    primarySimulation_->Shutdown();
    return false;
  }

  executionState_ = SimulationExecutionState::Stopped;
  pendingTicks_ = 0;
  initialized_ = true;
  lastError_.clear();
  return true;
}

void SimulationRuntime::Shutdown() {
  if (!initialized_ && primarySimulation_ == nullptr) {
    return;
  }

  if (scenarioExecutor_ != nullptr) {
    FinishScenario();
  }
  telemetryRecording_.Stop();
  executionState_ = SimulationExecutionState::Stopped;
  pendingTicks_ = 0;

  if (baselineSimulation_ != nullptr) {
    baselineSimulation_->Shutdown();
  }
  if (primarySimulation_ != nullptr) {
    primarySimulation_->Shutdown();
  }
  initialized_ = false;
}

void SimulationRuntime::Start() {
  if (initialized_ && executionState_ == SimulationExecutionState::Stopped) {
    pendingTicks_ = 0;
    executionState_ = SimulationExecutionState::Running;
  }
}

void SimulationRuntime::Stop() {
  if (scenarioExecutor_ != nullptr) {
    FinishScenario();
    return;
  }
  pendingTicks_ = 0;
  executionState_ = SimulationExecutionState::Stopped;
}

void SimulationRuntime::Pause() {
  if (executionState_ == SimulationExecutionState::Running) {
    executionState_ = SimulationExecutionState::Paused;
  }
}

void SimulationRuntime::Resume() {
  if (executionState_ == SimulationExecutionState::Paused) {
    pendingTicks_ = 0;
    executionState_ = SimulationExecutionState::Running;
  }
}

bool SimulationRuntime::Reset() {
  return scenarioExecutor_ == nullptr && ResetSimulations(nullptr);
}

bool SimulationRuntime::Reset(const InitialCondition &initialCondition) {
  return scenarioExecutor_ == nullptr && ResetSimulations(&initialCondition);
}

void SimulationRuntime::RequestTick() {
  if (executionState_ == SimulationExecutionState::Paused) {
    ++pendingTicks_;
  }
}

bool SimulationRuntime::Tick() {
  const bool isPaused = executionState_ == SimulationExecutionState::Paused;
  if (isPaused && pendingTicks_ == 0) {
    return true;
  }
  if (executionState_ != SimulationExecutionState::Running && !isPaused) {
    return true;
  }

  const double sharedDtSec = primarySimulation_->GetTickSizeSec();
  ScenarioStepResult scenarioStep;
  if (scenarioExecutor_ != nullptr) {
    scenarioStep = scenarioExecutor_->Step();
    if (!scenarioStep.succeeded) {
      lastError_ = scenarioExecutor_->GetLastError();
      std::cerr << "Scenario step failed: " << lastError_ << '\n';
      return false;
    }
    RecordPendingScenarioCommandEvent();
  } else {
    if (!SynchronizeBaselineControlState()) {
      return false;
    }
    if (!primarySimulation_->Step(sharedDtSec)) {
      lastError_ = GetSimulationError(primarySimulation_.get(),
          "Primary simulation step failed.");
      return false;
    }
    if (baselineSimulation_ != nullptr
        && !baselineSimulation_->Step(sharedDtSec)) {
      lastError_ = GetSimulationError(baselineSimulation_.get(),
          "Baseline simulation step failed.");
      return false;
    }
  }

  telemetryRecording_.Consume(primarySimulation_->GetTime(),
      primarySimulation_->GetTelemetryRegistry(),
      scenarioExecutor_ == nullptr && baselineSimulation_ != nullptr
          ? &baselineSimulation_->GetTelemetryRegistry()
          : nullptr);

  if (scenarioExecutor_ != nullptr && scenarioStep.completed) {
    FinishScenario();
  }
  if (isPaused) {
    --pendingTicks_;
  }

  lastError_.clear();
  return true;
}

bool SimulationRuntime::RunExecution(const ResolvedExecutionSpec &execution) {
  const SimulationScenario &scenario = execution.scenario;
  if (!initialized_ || executionState_ != SimulationExecutionState::Stopped
      || primarySimulation_ == nullptr || !primarySimulation_->IsInitialized()
      || (baselineSimulation_ != nullptr
          && !baselineSimulation_->IsInitialized())) {
    return false;
  }

  std::string validationError;
  if (!ValidateSimulationScenario(scenario, &validationError)) {
    lastError_ = validationError;
    return false;
  }
  if (scenario.aircraft != config_.aircraftName
      || std::abs(scenario.dtSec - config_.GetDT()) > 1.0e-12) {
    if (!ReinitializeForScenario(scenario)) {
      return false;
    }
  }
  if (!SelectExecutionVariant(execution.variant)) {
    return false;
  }
  auto executor = std::make_unique<ScenarioExecutor>(*primarySimulation_);
  if (!executor->Start(scenario, primarySimulation_->GetTickSizeSec())) {
    lastError_ = executor->GetLastError();
    RestoreInteractiveSimulationOrder();
    return false;
  }
  scenarioExecutor_ = std::move(executor);
  resolvedExecution_ = execution;
  pendingScenarioEvents_.clear();
  const telemetry::recording::ScenarioEvent startEvent{
      .simulationTimeSec = primarySimulation_->GetTime(),
      .type = "scenario_start",
      .targetRollRad = std::nullopt,
  };
  telemetryRecording_.RecordScenarioEvent(startEvent);
  pendingScenarioEvents_.push_back(startEvent);
  RecordPendingScenarioCommandEvent();
  pendingTicks_ = 0;
  executionState_ = SimulationExecutionState::Running;
  lastError_.clear();
  return true;
}

std::optional<ScenarioExecutionStatus>
SimulationRuntime::GetScenarioStatus() const {
  if (scenarioExecutor_ == nullptr
      || scenarioExecutor_->GetScenario() == nullptr) {
    return std::nullopt;
  }
  const SimulationScenario &scenario = *scenarioExecutor_->GetScenario();
  return ScenarioExecutionStatus{
      .name = scenario.name,
      .elapsedSec = scenarioExecutor_->GetElapsedSec(),
      .durationSec = scenario.durationSec,
  };
}

void SimulationRuntime::SetAutomaticSimulationHz(double hz) {
  if (!std::isfinite(hz)) {
    return;
  }
  automaticSimulationHz_ = ClampAutomaticSimulationHz(hz);
  maximumSimulationSpeedEnabled_ = false;
}

double SimulationRuntime::GetAutomaticSimulationHz() const {
  return automaticSimulationHz_;
}

void SimulationRuntime::SetMaximumSimulationSpeedEnabled(bool enabled) {
  maximumSimulationSpeedEnabled_ = enabled;
}

bool SimulationRuntime::IsMaximumSimulationSpeedEnabled() const {
  return maximumSimulationSpeedEnabled_;
}

SimulationStatus SimulationRuntime::GetStatus() const {
  return SimulationStatus{
      .executionState = executionState_,
      .scenario = GetScenarioStatus(),
      .automaticSimulationHz = automaticSimulationHz_,
      .maximumSimulationSpeedEnabled = maximumSimulationSpeedEnabled_,
      .pendingTickCount = pendingTicks_,
      .initialized = initialized_,
      .baselineAvailable = baselineSimulation_ != nullptr
                           && baselineSimulation_->IsInitialized(),
      .lastError = lastError_,
  };
}

SimulationSnapshot SimulationRuntime::GetSnapshot() const {
  SimulationSnapshot snapshot{
      .status = GetStatus(),
      .config = config_,
      .appliedExecution = resolvedExecution_,
      .telemetryRecording = GetTelemetryRecordingStatus(),
  };
  if (primarySimulation_ != nullptr && primarySimulation_->IsInitialized()) {
    snapshot.defaultInitialCondition =
        primarySimulation_->GetDefaultInitialCondition();
    snapshot.primary = CaptureSnapshot(*primarySimulation_);
    snapshot.primaryAutopilot = CaptureAutopilotSnapshot(*primarySimulation_);
    if (const gnc::TrimResult *result =
            primarySimulation_->GetTrimService().GetResult()) {
      snapshot.trim.result = *result;
    }
    snapshot.linearization = CaptureLinearizationSnapshot();
  }
  if (baselineSimulation_ != nullptr && baselineSimulation_->IsInitialized()) {
    snapshot.baseline = CaptureSnapshot(*baselineSimulation_);
    snapshot.baselineAutopilot = CaptureAutopilotSnapshot(*baselineSimulation_);
  }
  return snapshot;
}

std::uint64_t SimulationRuntime::GetTelemetryVersion(
    SimulationSlot slot) const {
  const Simulation *simulation = GetSimulation(slot);
  return simulation != nullptr && simulation->IsInitialized()
             ? simulation->GetTelemetryRegistry().GetVersion()
             : 0;
}

telemetry::TelemetryFrame SimulationRuntime::GetLatestTelemetryFrame(
    SimulationSlot slot) const {
  const Simulation *simulation = GetSimulation(slot);
  return simulation != nullptr && simulation->IsInitialized()
             ? simulation->GetTelemetryRegistry().CaptureLatestFrame()
             : telemetry::TelemetryFrame{};
}

telemetry::TelemetrySnapshot SimulationRuntime::GetTelemetrySnapshot(
    SimulationSlot slot) const {
  const Simulation *simulation = GetSimulation(slot);
  return simulation != nullptr && simulation->IsInitialized()
             ? simulation->GetTelemetryRegistry().CaptureSnapshot()
             : telemetry::TelemetrySnapshot{};
}

double SimulationRuntime::GetSimulationTimeSec() const {
  return primarySimulation_ != nullptr && primarySimulation_->IsInitialized()
             ? primarySimulation_->GetTime()
             : 0.0;
}

std::optional<telemetry::recording::TelemetrySourceFrame>
SimulationRuntime::CaptureRecordingSource() const {
  if (primarySimulation_ == nullptr || !primarySimulation_->IsInitialized()) {
    return std::nullopt;
  }
  return telemetry::recording::TelemetryRecordingService::CaptureSource(
      primarySimulation_->GetTelemetryRegistry(),
      primarySimulation_->GetTime());
}

std::vector<telemetry::recording::ScenarioEvent>
SimulationRuntime::TakeScenarioEvents() {
  return std::exchange(pendingScenarioEvents_, {});
}

bool SimulationRuntime::SetManualControl(const control::ControlInput &input) {
  if (!initialized_ || primarySimulation_ == nullptr) {
    return false;
  }
  auto *manager =
      primarySimulation_->GetComponent<control::FlightControlManager>();
  return manager != nullptr
         && manager->GetManualController().SetCommandedInput(input);
}

bool SimulationRuntime::SetPrimaryRollHoldConfig(
    const PrimaryRollHoldConfig &config) {
  if (!initialized_ || scenarioExecutor_ != nullptr) {
    return false;
  }
  auto *manager =
      primarySimulation_->GetComponent<control::FlightControlManager>();
  auto *autopilot = manager != nullptr ? dynamic_cast<gnc::MyAutopilot *>(
                                             &manager->GetAutopilot())
                                       : nullptr;
  if (autopilot == nullptr) {
    return false;
  }

  const gnc::RollHoldSettings previous = autopilot->GetRollHoldSettings();
  gnc::RollHoldSettings settings = previous;
  settings.targetRollRad = config.targetRollRad;
  settings.attitudeLoop.proportionalGain = config.rollAngleProportionalGain;
  settings.rateLoop.proportionalGain = config.rollRateProportionalGain;
  autopilot->SetRollHoldSettings(settings);
  autopilot->SetRollHoldEnabled(config.enabled);
  manager->SetMode(config.enabled ? control::FlightControlMode::Autopilot
                                  : control::FlightControlMode::Manual);

  if (previous.attitudeLoop.proportionalGain
          != settings.attitudeLoop.proportionalGain
      || previous.rateLoop.proportionalGain
             != settings.rateLoop.proportionalGain) {
    telemetryRecording_.RecordPrimarySettings({
        .simulationTimeSec = primarySimulation_->GetTime(),
        .rollAngleProportionalGain = settings.attitudeLoop.proportionalGain,
        .rollRateProportionalGain = settings.rateLoop.proportionalGain,
    });
  }
  return true;
}

bool SimulationRuntime::SetBaselineRollHoldConfig(
    const BaselineRollHoldConfig &config) {
  if (!initialized_ || scenarioExecutor_ != nullptr
      || baselineSimulation_ == nullptr) {
    return false;
  }
  auto *manager =
      baselineSimulation_->GetComponent<control::FlightControlManager>();
  auto *autopilot = manager != nullptr ? dynamic_cast<gnc::PX4Autopilot *>(
                                             &manager->GetAutopilot())
                                       : nullptr;
  if (autopilot == nullptr) {
    return false;
  }

  const gnc::Px4RollHoldReferenceSettings previous =
      autopilot->GetRollHoldSettings();
  gnc::Px4RollHoldReferenceSettings settings = previous;
  settings.timeConstantSec = config.timeConstantSec;
  settings.maximumRollRateRadPerSec = config.maximumRollRateRadPerSec;
  settings.rateProportionalGain = config.rateProportionalGain;
  settings.rateIntegralGain = config.rateIntegralGain;
  settings.rateDerivativeGain = config.rateDerivativeGain;
  settings.rateFeedForwardGain = config.rateFeedForwardGain;
  settings.integratorLimit = config.integratorLimit;
  autopilot->SetRollHoldSettings(settings);
  autopilot->SetTargetRollRad(config.targetRollRad);
  autopilot->SetRollHoldEnabled(config.enabled);
  manager->SetMode(config.enabled ? control::FlightControlMode::Autopilot
                                  : control::FlightControlMode::Manual);

  if (previous.timeConstantSec != settings.timeConstantSec
      || previous.maximumRollRateRadPerSec != settings.maximumRollRateRadPerSec
      || previous.rateProportionalGain != settings.rateProportionalGain
      || previous.rateIntegralGain != settings.rateIntegralGain
      || previous.rateDerivativeGain != settings.rateDerivativeGain
      || previous.rateFeedForwardGain != settings.rateFeedForwardGain
      || previous.integratorLimit != settings.integratorLimit) {
    telemetryRecording_.RecordBaselineSettings({
        .simulationTimeSec = baselineSimulation_->GetTime(),
        .rollTimeConstantSec = settings.timeConstantSec,
        .maximumRollRateRadPerSec = settings.maximumRollRateRadPerSec,
        .rateProportionalGain = settings.rateProportionalGain,
        .rateIntegralGain = settings.rateIntegralGain,
        .rateDerivativeGain = settings.rateDerivativeGain,
        .rateFeedForwardGain = settings.rateFeedForwardGain,
        .integratorLimit = settings.integratorLimit,
    });
  }
  return true;
}

bool SimulationRuntime::RunTrim(const gnc::TrimRequest &request,
    bool fromCurrentState) {
  if (!initialized_ || primarySimulation_ == nullptr
      || scenarioExecutor_ != nullptr) {
    return false;
  }
  auto *manager =
      primarySimulation_->GetComponent<control::FlightControlManager>();
  if (manager == nullptr) {
    return false;
  }

  const bool resume = executionState_ == SimulationExecutionState::Running;
  Pause();
  Aircraft &aircraft = primarySimulation_->GetAircraft();
  gnc::TrimService &trimService = primarySimulation_->GetTrimService();
  const bool computed =
      fromCurrentState ? trimService.ComputeCurrentState(aircraft, request.mode)
                       : trimService.Compute(aircraft, request);
  const bool applied = computed && trimService.ApplyStored(aircraft);
  if (applied) {
    if (const gnc::TrimResult *result = trimService.GetResult()) {
      manager->SynchronizeWithTrimResult(aircraft, *result);
    }
  } else {
    lastError_ = "Trim request failed.";
  }
  if (resume) {
    Resume();
  }
  return applied;
}

bool SimulationRuntime::SetAutomaticLinearizationEnabled(bool enabled) {
  if (!initialized_ || primarySimulation_ == nullptr) {
    return false;
  }
  auto *manager =
      primarySimulation_->GetComponent<control::FlightControlManager>();
  auto *analysis = manager != nullptr ? dynamic_cast<gnc::IAutopilotAnalysis *>(
                                            &manager->GetAutopilot())
                                      : nullptr;
  if (analysis == nullptr) {
    return false;
  }
  analysis->SetAutomaticLinearizationEnabled(enabled);
  return true;
}

bool SimulationRuntime::StartTelemetryRecording() {
  if (!initialized_) {
    return false;
  }

  telemetry::recording::RecordingMetadata metadata;
  metadata.aircraft = config_.aircraftName;
  metadata.simulationDtSec = config_.GetDT();
  metadata.primaryAutopilot = "MyAutopilot";
  metadata.baselineAutopilot =
      baselineSimulation_ != nullptr ? "PX4Autopilot" : "none";
  if (scenarioExecutor_ != nullptr
      && scenarioExecutor_->GetScenario() != nullptr) {
    const SimulationScenario &scenario = *scenarioExecutor_->GetScenario();
    metadata.scenarioName = scenario.name;
    if (resolvedExecution_) {
      metadata.scenarioFile = resolvedExecution_->source.file;
      metadata.scenarioDigest = resolvedExecution_->source.digestSha256;
      metadata.executionVariant =
          std::string(ToString(resolvedExecution_->variant));
      metadata.primaryAutopilot = metadata.executionVariant;
    }
    metadata.scenarioDurationSec = scenario.durationSec;
  } else {
    metadata.scenarioName = "interactive";
  }
  metadata.executionMode = "single";
  if (!telemetryRecording_.StartDefault(metadata, metadata.scenarioName)) {
    return false;
  }

  if (auto *manager =
          primarySimulation_->GetComponent<control::FlightControlManager>()) {
    if (auto *autopilot =
            dynamic_cast<gnc::MyAutopilot *>(&manager->GetAutopilot())) {
      const gnc::RollHoldSettings &settings = autopilot->GetRollHoldSettings();
      telemetryRecording_.RecordPrimarySettings({
          .simulationTimeSec = primarySimulation_->GetTime(),
          .rollAngleProportionalGain = settings.attitudeLoop.proportionalGain,
          .rollRateProportionalGain = settings.rateLoop.proportionalGain,
      });
    }
  }
  if (baselineSimulation_ != nullptr) {
    if (auto *manager = baselineSimulation_
            ->GetComponent<control::FlightControlManager>()) {
      if (auto *autopilot =
              dynamic_cast<gnc::PX4Autopilot *>(&manager->GetAutopilot())) {
        const gnc::Px4RollHoldReferenceSettings &settings =
            autopilot->GetRollHoldSettings();
        telemetryRecording_.RecordBaselineSettings({
            .simulationTimeSec = baselineSimulation_->GetTime(),
            .rollTimeConstantSec = settings.timeConstantSec,
            .maximumRollRateRadPerSec = settings.maximumRollRateRadPerSec,
            .rateProportionalGain = settings.rateProportionalGain,
            .rateIntegralGain = settings.rateIntegralGain,
            .rateDerivativeGain = settings.rateDerivativeGain,
            .rateFeedForwardGain = settings.rateFeedForwardGain,
            .integratorLimit = settings.integratorLimit,
        });
      }
    }
  }
  return true;
}

bool SimulationRuntime::StartTelemetryRecording(
    const std::filesystem::path &path,
    const telemetry::recording::RecordingMetadata &metadata) {
  telemetry::recording::TelemetryRecordingConfig recordingConfig;
  recordingConfig.outputPath = path;
  recordingConfig.recordPrimary = true;
  recordingConfig.recordBaseline = baselineSimulation_ != nullptr;
  if (!telemetryRecording_.Start(recordingConfig, metadata)) {
    return false;
  }
  if (scenarioExecutor_ != nullptr
      && scenarioExecutor_->GetScenario() != nullptr) {
    const SimulationScenario &scenario = *scenarioExecutor_->GetScenario();
    telemetryRecording_.RecordScenarioEvent({
        .simulationTimeSec = primarySimulation_->GetTime(),
        .type = "scenario_start",
        .targetRollRad = std::nullopt,
    });
  }
  return true;
}

void SimulationRuntime::StopTelemetryRecording() { telemetryRecording_.Stop(); }

telemetry::recording::RecordingStatus
SimulationRuntime::GetTelemetryRecordingStatus() const {
  return telemetryRecording_.GetStatus();
}

bool SimulationRuntime::ResetSimulations(
    const InitialCondition *initialCondition) {
  if (!initialized_ || primarySimulation_ == nullptr) {
    return false;
  }
  const auto reset = [initialCondition](Simulation &simulation) {
    return initialCondition != nullptr ? simulation.Reset(*initialCondition)
                                       : simulation.Reset();
  };
  if (!reset(*primarySimulation_)) {
    lastError_ = GetSimulationError(primarySimulation_.get(),
        "Failed to reset primary simulation.");
    return false;
  }
  if (baselineSimulation_ != nullptr && !reset(*baselineSimulation_)) {
    lastError_ = GetSimulationError(baselineSimulation_.get(),
        "Failed to reset baseline simulation.");
    return false;
  }
  lastError_.clear();
  return true;
}

bool SimulationRuntime::SynchronizeBaselineControlState() {
  if (baselineSimulation_ == nullptr) {
    return true;
  }
  auto *primaryManager =
      primarySimulation_->GetComponent<control::FlightControlManager>();
  auto *baselineManager =
      baselineSimulation_->GetComponent<control::FlightControlManager>();
  if (primaryManager == nullptr || baselineManager == nullptr) {
    lastError_ = "Failed to synchronize baseline flight controls.";
    return false;
  }
  baselineManager->GetManualController().SetCommandedInput(
      primaryManager->GetManualController().GetCommandedInput());
  return true;
}

void SimulationRuntime::FinishScenario() {
  if (scenarioExecutor_ != nullptr) {
    const telemetry::recording::ScenarioEvent endEvent{
        .simulationTimeSec = primarySimulation_->GetTime(),
        .type = "scenario_end",
    };
    telemetryRecording_.RecordScenarioEvent(endEvent);
    pendingScenarioEvents_.push_back(endEvent);
    scenarioExecutor_->Stop();
    scenarioExecutor_.reset();
  }
  RestoreInteractiveSimulationOrder();
  pendingTicks_ = 0;
  executionState_ = SimulationExecutionState::Stopped;
}

void SimulationRuntime::RecordPendingScenarioCommandEvent() {
  if (scenarioExecutor_ == nullptr || primarySimulation_ == nullptr) {
    return;
  }
  for (const auto &activation : scenarioExecutor_->TakeCommandActivations()) {
    const telemetry::recording::ScenarioEvent event{
        .simulationTimeSec = activation.simulationTimeSec,
        .type = "roll_command_changed",
        .targetRollRad = activation.targetRollRad,
    };
    telemetryRecording_.RecordScenarioEvent(event);
    pendingScenarioEvents_.push_back(event);
  }
}

bool SimulationRuntime::SelectExecutionVariant(ExecutionVariant variant) {
  const auto identify = [](Simulation *simulation) {
    if (simulation == nullptr) {
      return std::optional<ExecutionVariant>{};
    }
    auto *manager = simulation->GetComponent<control::FlightControlManager>();
    return manager == nullptr ? std::optional<ExecutionVariant>{}
                              : ExecutionVariantResolver::IdentifyVariant(
                                    manager->GetAutopilot());
  };
  if (identify(primarySimulation_.get()) == variant) {
    return true;
  }
  if (identify(baselineSimulation_.get()) == variant) {
    std::swap(primarySimulation_, baselineSimulation_);
    scenarioSimulationSwapped_ = true;
    return true;
  }
  lastError_ = "initialized runtime does not contain execution variant '"
               + std::string(ToString(variant)) + "'";
  return false;
}

bool SimulationRuntime::ReinitializeForScenario(
    const SimulationScenario &scenario) {
  if (baselineSimulation_ != nullptr) {
    baselineSimulation_->Shutdown();
  }
  primarySimulation_->Shutdown();
  initialized_ = false;
  SimulationConfig scenarioConfig = config_;
  scenarioConfig.aircraftName = scenario.aircraft;
  scenarioConfig.simulationHz = 1.0 / scenario.dtSec;
  return Initialize(scenarioConfig);
}

void SimulationRuntime::RestoreInteractiveSimulationOrder() {
  if (scenarioSimulationSwapped_) {
    std::swap(primarySimulation_, baselineSimulation_);
    scenarioSimulationSwapped_ = false;
  }
}

SimulationInstanceSnapshot SimulationRuntime::CaptureSnapshot(
    const Simulation &simulation) const {
  const Aircraft &aircraft = simulation.GetAircraft();
  return SimulationInstanceSnapshot{
      .aircraft = aircraft.GetAircraftState(),
      .aircraftDerivative = aircraft.GetAircraftStateDerivative(),
      .fdmState = aircraft.ExtractFDMState(FDMStateFlags::All),
      .controlInput = aircraft.GetControls().GetInput(),
      .engines = aircraft.GetEngines().GetEngineStates(),
      .currentCondition = simulation.GetCurrentCondition(),
      .pitchTrim = aircraft.GetControls().GetPitchTrim(),
      .available = true,
  };
}

AutopilotSnapshot SimulationRuntime::CaptureAutopilotSnapshot(
    const Simulation &simulation) const {
  AutopilotSnapshot snapshot;
  const auto *manager =
      simulation.GetComponent<control::FlightControlManager>();
  if (manager == nullptr) {
    return snapshot;
  }

  snapshot.available = true;
  snapshot.mode = manager->GetMode();
  snapshot.manualControl = manager->GetManualController().GetCommandedInput();
  const gnc::IAutopilot &strategy = manager->GetAutopilot();
  if (const auto *autopilot =
          dynamic_cast<const gnc::MyAutopilot *>(&strategy)) {
    snapshot.strategyName = "MyAutopilot";
    const gnc::RollHoldSettings &settings = autopilot->GetRollHoldSettings();
    snapshot.primaryRollHold = {
        .enabled = autopilot->IsRollHoldEnabled(),
        .targetRollRad = settings.targetRollRad,
        .rollAngleProportionalGain = settings.attitudeLoop.proportionalGain,
        .rollRateProportionalGain = settings.rateLoop.proportionalGain,
    };
  } else if (const auto *autopilot =
                 dynamic_cast<const gnc::PX4Autopilot *>(&strategy)) {
    snapshot.strategyName = "PX4Autopilot";
    const gnc::Px4RollHoldReferenceSettings &settings =
        autopilot->GetRollHoldSettings();
    const gnc::Px4RollHoldReferenceDiagnostics &diagnostics =
        autopilot->GetRollHoldDiagnostics();
    snapshot.baselineRollHold = {
        .enabled = autopilot->IsRollHoldEnabled(),
        .targetRollRad = autopilot->GetTargetRollRad(),
        .timeConstantSec = settings.timeConstantSec,
        .maximumRollRateRadPerSec = settings.maximumRollRateRadPerSec,
        .rateProportionalGain = settings.rateProportionalGain,
        .rateIntegralGain = settings.rateIntegralGain,
        .rateDerivativeGain = settings.rateDerivativeGain,
        .rateFeedForwardGain = settings.rateFeedForwardGain,
        .integratorLimit = settings.integratorLimit,
    };
    snapshot.baselineDiagnostics = {
        .aileronCommand = diagnostics.aileronCommand,
        .bodyRateSetpointRadPerSec = diagnostics.bodyRateSetpointRadPerSec,
        .rollErrorRad = diagnostics.rollErrorRad,
        .airspeedScaling = diagnostics.airspeedScaling,
    };
  } else {
    snapshot.strategyName = "Autopilot Strategy";
  }
  return snapshot;
}

LinearizationSnapshot SimulationRuntime::CaptureLinearizationSnapshot() const {
  LinearizationSnapshot snapshot;
  if (primarySimulation_ == nullptr) {
    return snapshot;
  }
  const auto *manager =
      primarySimulation_->GetComponent<control::FlightControlManager>();
  const auto *analysis = manager != nullptr
                             ? dynamic_cast<const gnc::IAutopilotAnalysis *>(
                                   &manager->GetAutopilot())
                             : nullptr;
  if (analysis == nullptr) {
    return snapshot;
  }

  snapshot.available = true;
  snapshot.automaticUpdatesEnabled =
      analysis->IsAutomaticLinearizationEnabled();
  snapshot.updateInProgress = analysis->IsLinearizationInProgress();
  snapshot.errorMessage = analysis->GetLinearizationErrorMessage();
  if (const gnc::LinearizationResult *result =
          analysis->GetLinearizationResult()) {
    snapshot.result = *result;
  }
  snapshot.dynamicModeHistory = analysis->GetDynamicModeHistory();
  return snapshot;
}

Simulation *SimulationRuntime::GetSimulation(SimulationSlot slot) {
  return slot == SimulationSlot::Primary ? primarySimulation_.get()
                                         : baselineSimulation_.get();
}

const Simulation *SimulationRuntime::GetSimulation(SimulationSlot slot) const {
  return slot == SimulationSlot::Primary ? primarySimulation_.get()
                                         : baselineSimulation_.get();
}
} // namespace sim
