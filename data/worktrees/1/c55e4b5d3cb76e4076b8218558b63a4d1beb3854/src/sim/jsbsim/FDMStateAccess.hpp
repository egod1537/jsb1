#pragma once

#include "sim/FDMState.hpp"

namespace JSBSim {
class FGFDMExec;
} // namespace JSBSim

namespace sim::jsbsim {
class FDMStateAccess {
public:
  explicit FDMStateAccess(JSBSim::FGFDMExec &fdmExec);

  FDMState Extract(FDMStateFlags flags) const;
  void Apply(const FDMState &state);

private:
  JSBSim::FGFDMExec &fdmExec_;
};
} // namespace sim::jsbsim
