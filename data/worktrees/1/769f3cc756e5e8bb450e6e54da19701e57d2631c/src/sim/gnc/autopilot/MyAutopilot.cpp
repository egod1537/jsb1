#include "sim/gnc/autopilot/MyAutopilot.hpp"

#include "sim/Aircraft.hpp"
#include "sim/FDMState.hpp"
#include "sim/Tick.hpp"
#include "sim/control/ControlInput.hpp"
#include "sim/gnc/ControlContext.hpp"
#include "sim/gnc/hold/PitchDynamics.hpp"
#include "sim/gnc/hold/RollDynamics.hpp"
#include "sim/linearization/AsyncAircraftLinearizer.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <utility>

namespace {
constexpr double LinearizationRefreshIntervalSec = 5.0;
}

namespace gnc {
MyAutopilot::MyAutopilot()
    : asyncLinearizer_(std::make_unique<sim::AsyncAircraftLinearizer>()) {
  AddController<RollHoldController>();
}

MyAutopilot::~MyAutopilot() = default;

Controller *MyAutopilot::FindController(const std::type_info &type) {
  for (const auto &controller : controllers_) {
    if (typeid(*controller) == type) {
      return controller.get();
    }
  }
  return nullptr;
}

const Controller *MyAutopilot::FindController(
    const std::type_info &type) const {
  for (const auto &controller : controllers_) {
    if (typeid(*controller) == type) {
      return controller.get();
    }
  }
  return nullptr;
}

void MyAutopilot::Reset() {
  ResetControllers();
  InvalidateLinearization();
}

control::ControlInput MyAutopilot::Update(sim::Aircraft &aircraft,
    const sim::Tick &tick, const control::ControlInput &passthroughCommand) {
  control::ControlInput input = passthroughCommand;

  RollHoldController *rollHold = GetController<RollHoldController>();
  const ControlContext context{};

  std::optional<double> customAileron;
  if (rollHold != nullptr) {
    customAileron = rollHold->OnTick(aircraft, tick, context);
    if (customAileron) {
      input.aileron = *customAileron;
    }
  }

  control::ClampControlInput(input);
  return input;
}

std::optional<RollDynamics> MyAutopilot::GetRollDynamics() const {
  if (!linearization_) {
    return std::nullopt;
  }

  const auto &A = linearization_->A;
  const auto &B = linearization_->B;

  const auto p = linearization_->FindStateIndex("P");
  const auto da = linearization_->FindInputIndex("DaCmd");

  if (!p || !da) {
    return std::nullopt;
  }

  return RollDynamics{
      .aPhi1 = -A(*p, *p),
      .aPhi2 = B(*p, *da),
  };
}

std::optional<PitchDynamics> MyAutopilot::GetPitchDynamics() const {
  if (!linearization_) {
    return std::nullopt;
  }

  const auto &A = linearization_->A;
  const auto &B = linearization_->B;

  const auto alpha = linearization_->FindStateIndex("Alpha");
  const auto q = linearization_->FindStateIndex("Q");
  const auto de = linearization_->FindInputIndex("DeCmd");

  const auto p = linearization_->FindStateIndex("P");
  const auto r = linearization_->FindStateIndex("R");

  if (!alpha || !q || !de) {
    return std::nullopt;
  }

  return PitchDynamics{
      .aTheta1 = -A(*q, *q),
      .aTheta2 = -A(*q, *alpha),
      .aTheta3 = B(*q, *de),
  };
}

std::optional<YawDynamics> MyAutopilot::GetYawDynamics() const {
  if (!linearization_) {
    return std::nullopt;
  }

  const auto &A = linearization_->A;
  const auto &B = linearization_->B;

  const auto beta = linearization_->FindStateIndex("Beta");
  const auto r = linearization_->FindStateIndex("R");
  const auto dr = linearization_->FindInputIndex("DrCmd");

  if (!beta || !r || !dr) {
    return std::nullopt;
  }

  return YawDynamics{
      .aBetaBeta = A(*beta, *beta),
      .aBetaR = A(*beta, *r),
      .aRBeta = A(*r, *beta),
      .aRR = A(*r, *r),
      .bBetaRudder = B(*beta, *dr),
      .bRRudder = B(*r, *dr),
  };
}

void MyAutopilot::UpdateLinearization(sim::Aircraft &aircraft,
    const sim::Tick &tick) {
  PollLinearization();

  if (!automaticLinearizationEnabled_) {
    return;
  }

  if (!lastLinearizationCycleSimTimeSec_
      || tick.simTimeSec < *lastLinearizationCycleSimTimeSec_) {
    lastLinearizationCycleSimTimeSec_ = tick.simTimeSec;
    return;
  }

  if (asyncLinearizer_->IsBusy()) {
    return;
  }

  const bool refreshDue = tick.simTimeSec - *lastLinearizationCycleSimTimeSec_
                          >= LinearizationRefreshIntervalSec;
  if (refreshDue) {
    SubmitLinearization(aircraft, tick.simTimeSec);
  }
}

bool MyAutopilot::IsAutomaticLinearizationEnabled() const {
  return automaticLinearizationEnabled_;
}

void MyAutopilot::SetAutomaticLinearizationEnabled(bool enabled) {
  if (automaticLinearizationEnabled_ == enabled) {
    return;
  }

  automaticLinearizationEnabled_ = enabled;
  lastLinearizationCycleSimTimeSec_.reset();
  if (!enabled) {
    ++linearizationGeneration_;
  }
}

void MyAutopilot::PollLinearization() {
  if (auto completion = asyncLinearizer_->TakeCompletion()) {
    if (completion->generation == linearizationGeneration_) {
      if (completion->linearization) {
        linearization_ = std::move(completion->linearization);
        const double linearizationTimeSec =
            lastLinearizationCycleSimTimeSec_.value_or(0.0);
        DynamicModeAnalysis analysis =
            DynamicModeAnalyzer::Analyze(*linearization_, linearizationTimeSec);
        if (analysis.valid) {
          dynamicModeHistory_.Push({
              .simulationTimeSec = linearizationTimeSec,
              .analysis = std::move(analysis),
          });
        }
        linearizationErrorMessage_.clear();
      } else if (!completion->errorMessage.empty()) {
        std::cerr << "[MyAutopilot] " << completion->errorMessage << '\n';
        linearizationErrorMessage_ = std::move(completion->errorMessage);
      }
    }
  }
}

bool MyAutopilot::IsLinearizationInProgress() const {
  return asyncLinearizer_->IsBusy();
}

const LinearizationResult *MyAutopilot::GetLinearizationResult() const {
  return linearization_ ? &*linearization_ : nullptr;
}

const DynamicModeAnalysis *MyAutopilot::GetDynamicModeAnalysis() const {
  const auto &snapshots = dynamicModeHistory_.GetSnapshots();
  return snapshots.empty() ? nullptr : &snapshots.back().analysis;
}

const DynamicModeHistory &MyAutopilot::GetDynamicModeHistory() const {
  return dynamicModeHistory_;
}

std::string_view MyAutopilot::GetLinearizationErrorMessage() const {
  return linearizationErrorMessage_;
}

bool MyAutopilot::SubmitLinearization(sim::Aircraft &aircraft,
    double simulationTimeSec) {
  if (asyncLinearizer_->IsBusy()) {
    return false;
  }

  sim::FDMState sourceState = aircraft.ExtractFDMState(sim::FDMStateFlags::All);
  if (asyncLinearizer_->Submit(linearizationGeneration_,
          aircraft.GetConfig(),
          aircraft.GetCurrentCondition(),
          std::move(sourceState))) {
    lastLinearizationCycleSimTimeSec_ = simulationTimeSec;
    linearizationErrorMessage_.clear();
    return true;
  }

  return false;
}

void MyAutopilot::InvalidateLinearization() {
  linearization_.reset();
  dynamicModeHistory_.Clear();
  linearizationErrorMessage_.clear();
  lastLinearizationCycleSimTimeSec_.reset();
  ++linearizationGeneration_;
}

void MyAutopilot::SynchronizeTrimReferences(sim::Aircraft &,
    const TrimResult &trimResult) {
  ResetControllers();
  SyncControllerTrimReferences(trimResult);
  InvalidateLinearization();
}

void MyAutopilot::ResetControllers() {
  for (const auto &controller : controllers_) {
    controller->Reset();
  }
}

void MyAutopilot::SyncControllerTrimReferences(const TrimResult &result) {
  if (auto *rollHold = GetController<RollHoldController>()) {
    rollHold->SetTrimAileron(result.aileron);
  }
}

bool MyAutopilot::IsRollHoldEnabled() const {
  const auto *rollHold = GetController<RollHoldController>();
  return rollHold != nullptr && rollHold->IsEnabled();
}

void MyAutopilot::SetRollHoldEnabled(bool enabled) {
  if (auto *rollHold = GetController<RollHoldController>()) {
    rollHold->SetEnabled(enabled);
  }
}

double MyAutopilot::GetTargetRollRad() const {
  return GetRollHoldSettings().targetRollRad;
}

void MyAutopilot::SetTargetRollRad(double targetRollRad) {
  RollHoldSettings settings = GetRollHoldSettings();
  settings.targetRollRad = targetRollRad;
  SetRollHoldSettings(settings);
}

void MyAutopilot::SetRollHoldSettings(const RollHoldSettings &settings) {
  if (auto *rollHold = GetController<RollHoldController>()) {
    rollHold->SetSettings(settings);
  }
}

const RollHoldSettings &MyAutopilot::GetRollHoldSettings() const {
  static const RollHoldSettings DefaultSettings{};
  const auto *rollHold = GetController<RollHoldController>();
  return rollHold != nullptr ? rollHold->GetSettings() : DefaultSettings;
}

} // namespace gnc
