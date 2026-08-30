#include "sim/jsbsim/ControlSystem.hpp"

#include <FGFDMExec.h>

namespace {
constexpr const char *ElevatorCommand = "fcs/elevator-cmd-norm";
constexpr const char *PitchTrimCommand = "fcs/pitch-trim-cmd-norm";
constexpr const char *AileronCommand = "fcs/aileron-cmd-norm";
constexpr const char *RudderCommand = "fcs/rudder-cmd-norm";
constexpr const char *ThrottleCommand = "fcs/throttle-cmd-norm";
} // namespace

namespace sim::jsbsim {
ControlSystem::ControlSystem(JSBSim::FGFDMExec &fdmExec)
    : fdmExec_(fdmExec) {}

const control::ControlInput &ControlSystem::GetInput() const { return input_; }

void ControlSystem::SetInput(const control::ControlInput &input) {
  input_ = input;
  control::ClampControlInput(input_);
}

double ControlSystem::GetElevator() const { return input_.elevator; }

bool ControlSystem::SetElevator(double value) {
  return control::SetControlAxisValue(
      input_, control::ControlAxis::Elevator, value);
}

double ControlSystem::GetAileron() const { return input_.aileron; }

bool ControlSystem::SetAileron(double value) {
  return control::SetControlAxisValue(
      input_, control::ControlAxis::Aileron, value);
}

double ControlSystem::GetRudder() const { return input_.rudder; }

bool ControlSystem::SetRudder(double value) {
  return control::SetControlAxisValue(
      input_, control::ControlAxis::Rudder, value);
}

double ControlSystem::GetThrottle() const { return input_.throttle; }

bool ControlSystem::SetThrottle(double value) {
  return control::SetControlAxisValue(
      input_, control::ControlAxis::Throttle, value);
}

control::ControlInput ControlSystem::GetAppliedInput() const {
  return {
      .elevator = fdmExec_.GetPropertyValue(ElevatorCommand),
      .aileron = fdmExec_.GetPropertyValue(AileronCommand),
      .rudder = fdmExec_.GetPropertyValue(RudderCommand),
      .throttle = fdmExec_.GetPropertyValue(ThrottleCommand),
  };
}

double ControlSystem::GetPitchTrim() const {
  return fdmExec_.GetPropertyValue(PitchTrimCommand);
}

void ControlSystem::SetPitchTrim(double value) {
  fdmExec_.SetPropertyValue(PitchTrimCommand, value);
}

void ControlSystem::Reset() {
  input_ = {};
  SetPitchTrim(0.0);
  Apply();
}

void ControlSystem::Apply() {
  fdmExec_.SetPropertyValue(ElevatorCommand, input_.elevator);
  fdmExec_.SetPropertyValue(AileronCommand, input_.aileron);
  fdmExec_.SetPropertyValue(RudderCommand, input_.rudder);
  fdmExec_.SetPropertyValue(ThrottleCommand, input_.throttle);
}
} // namespace sim::jsbsim
