#include "sim/gnc/autopilot/PX4Autopilot.hpp"

#include "sim/Aircraft.hpp"
#include "sim/gnc/TrimTypes.hpp"

#include <algorithm>

namespace gnc {
void PX4Autopilot::Reset() { rollHoldController_.Reset(); }

control::ControlInput PX4Autopilot::Update(sim::Aircraft &aircraft,
    const sim::Tick &tick, const control::ControlInput &passthroughCommand) {
  control::ControlInput input = passthroughCommand;
  if (const auto aileronCommand =
          rollHoldController_.OnTick(aircraft, tick, targetRollRad_)) {
    input.aileron = *aileronCommand;
  }
  control::ClampControlInput(input);
  return input;
}

bool PX4Autopilot::IsRollHoldEnabled() const {
  return rollHoldController_.IsEnabled();
}

void PX4Autopilot::SetRollHoldEnabled(bool enabled) {
  rollHoldController_.SetEnabled(enabled);
}

double PX4Autopilot::GetTargetRollRad() const { return targetRollRad_; }

void PX4Autopilot::SetTargetRollRad(double targetRollRad) {
  targetRollRad_ = targetRollRad;
}

const Px4RollHoldReferenceSettings &PX4Autopilot::GetRollHoldSettings() const {
  return rollHoldController_.GetSettings();
}

void PX4Autopilot::SetRollHoldSettings(
    const Px4RollHoldReferenceSettings &settings) {
  rollHoldController_.SetSettings(settings);
}

const Px4RollHoldReferenceDiagnostics &
PX4Autopilot::GetRollHoldDiagnostics() const {
  return rollHoldController_.GetDiagnostics();
}

void PX4Autopilot::SynchronizeTrimReferences(sim::Aircraft &aircraft,
    const TrimResult &trimResult) {
  Px4RollHoldReferenceSettings settings = rollHoldController_.GetSettings();
  settings.trimAirspeedMps =
      std::max(aircraft.GetProperties().CalibratedAirspeed().Mps(), 0.1);
  settings.trimRollCommand = trimResult.aileron;
  rollHoldController_.SetSettings(settings);
}

Controller *PX4Autopilot::FindController(const std::type_info &type) {
  return type == typeid(Px4RollHoldReferenceController) ? &rollHoldController_
                                                        : nullptr;
}

const Controller *PX4Autopilot::FindController(
    const std::type_info &type) const {
  return type == typeid(Px4RollHoldReferenceController) ? &rollHoldController_
                                                        : nullptr;
}
} // namespace gnc
