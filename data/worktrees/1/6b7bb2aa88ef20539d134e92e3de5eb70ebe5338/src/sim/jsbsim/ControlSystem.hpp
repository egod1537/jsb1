#pragma once

#include "sim/control/ControlInput.hpp"

namespace JSBSim {
class FGFDMExec;
} // namespace JSBSim

namespace sim::jsbsim {
class ControlSystem {
public:
  explicit ControlSystem(JSBSim::FGFDMExec &fdmExec);

  // Grouped control command
  const control::ControlInput &GetInput() const;
  void SetInput(const control::ControlInput &input);

  // Primary control axes
  double GetElevator() const;
  bool SetElevator(double value);
  double GetAileron() const;
  bool SetAileron(double value);
  double GetRudder() const;
  bool SetRudder(double value);
  double GetThrottle() const;
  bool SetThrottle(double value);

  // Applied flight-model state
  control::ControlInput GetAppliedInput() const;
  double GetPitchTrim() const;
  void SetPitchTrim(double value);

  // Command lifecycle
  void Reset();
  void Apply();

private:
  JSBSim::FGFDMExec &fdmExec_;
  control::ControlInput input_;
};
} // namespace sim::jsbsim
