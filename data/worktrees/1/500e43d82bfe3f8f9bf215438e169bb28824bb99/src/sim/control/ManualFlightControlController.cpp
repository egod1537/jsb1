#include "sim/control/ManualFlightControlController.hpp"

namespace control {
void ManualFlightControlController::OnReset() { commandedInput_ = {}; }

ControlInput ManualFlightControlController::OnTick(sim::Aircraft &,
    const sim::Tick &) {
  return commandedInput_;
}

const ControlInput &ManualFlightControlController::GetCommandedInput() const {
  return commandedInput_;
}

bool ManualFlightControlController::SetCommandedInput(
    const ControlInput &input) {
  bool changed = false;
  changed = SetCommandedInput(ControlAxis::Elevator, input.elevator) || changed;
  changed = SetCommandedInput(ControlAxis::Aileron, input.aileron) || changed;
  changed = SetCommandedInput(ControlAxis::Rudder, input.rudder) || changed;
  changed = SetCommandedInput(ControlAxis::Throttle, input.throttle) || changed;
  return changed;
}

bool ManualFlightControlController::SetCommandedInput(ControlAxis axis,
    double value) {
  return SetControlAxisValue(commandedInput_, axis, value);
}

bool ManualFlightControlController::AdjustCommandedInput(ControlAxis axis,
    double delta) {
  return AdjustControlAxisValue(commandedInput_, axis, delta);
}
} // namespace control
