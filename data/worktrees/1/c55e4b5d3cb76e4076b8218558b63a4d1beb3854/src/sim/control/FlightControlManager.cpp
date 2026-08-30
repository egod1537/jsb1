#include "sim/control/FlightControlManager.hpp"

#include "sim/Aircraft.hpp"
#include "sim/gnc/autopilot/IAutopilotAnalysis.hpp"
#include "sim/gnc/autopilot/ITrimReferenceConsumer.hpp"

#include <stdexcept>
#include <utility>

namespace control {
FlightControlManager::FlightControlManager(
    std::unique_ptr<gnc::IAutopilot> autopilot)
    : autopilot_(std::move(autopilot)) {
  if (autopilot_ == nullptr) {
    throw std::invalid_argument("FlightControlManager requires an autopilot.");
  }
}

bool FlightControlManager::OnTick(const sim::Tick &tick) {
  sim::Aircraft &aircraft = GetAircraft();
  if (auto *analysis =
          dynamic_cast<gnc::IAutopilotAnalysis *>(autopilot_.get())) {
    analysis->UpdateLinearization(aircraft, tick);
  }
  if (const auto input = ProduceControlInput(aircraft, tick)) {
    aircraft.GetControls().SetInput(*input);
  }

  return true;
}

std::optional<ControlInput> FlightControlManager::ProduceControlInput(
    sim::Aircraft &aircraft, const sim::Tick &tick) {
  switch (mode_) {
  case FlightControlMode::None:
    return std::nullopt;
  case FlightControlMode::Manual:
    return manualController_.OnTick(aircraft, tick);
  case FlightControlMode::Autopilot:
    return autopilot_->Update(aircraft,
        tick,
        manualController_.OnTick(aircraft, tick));
  }

  return std::nullopt;
}

FlightControlMode FlightControlManager::GetMode() const { return mode_; }

void FlightControlManager::SetMode(FlightControlMode mode) { mode_ = mode; }

ManualFlightControlController &FlightControlManager::GetManualController() {
  return manualController_;
}

const ManualFlightControlController &
FlightControlManager::GetManualController() const {
  return manualController_;
}

gnc::IAutopilot &FlightControlManager::GetAutopilot() { return *autopilot_; }

const gnc::IAutopilot &FlightControlManager::GetAutopilot() const {
  return *autopilot_;
}

void FlightControlManager::ResetControllers() {
  mode_ = FlightControlMode::Manual;
  manualController_.OnReset();
  autopilot_->Reset();
}

void FlightControlManager::SynchronizeWithTrimResult(sim::Aircraft &aircraft,
    const gnc::TrimResult &trimResult) {
  manualController_.SetCommandedInput({
      .elevator = trimResult.elevator,
      .aileron = trimResult.aileron,
      .rudder = trimResult.rudder,
      .throttle = trimResult.throttle,
  });

  if (auto *trimConsumer =
          dynamic_cast<gnc::ITrimReferenceConsumer *>(autopilot_.get())) {
    trimConsumer->SynchronizeTrimReferences(aircraft, trimResult);
  }
}

} // namespace control
