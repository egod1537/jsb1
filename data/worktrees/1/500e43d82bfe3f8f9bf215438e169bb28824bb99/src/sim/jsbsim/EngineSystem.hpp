#pragma once

#include "sim/EngineState.hpp"

#include <cstddef>
#include <vector>

namespace JSBSim {
class FGFDMExec;
} // namespace JSBSim

namespace sim::jsbsim {
class EngineSystem {
public:
  explicit EngineSystem(JSBSim::FGFDMExec &fdmExec);

  // Engine state
  std::size_t GetEngineCount() const;
  EngineState GetEngineState(std::size_t index) const;
  std::vector<EngineState> GetEngineStates() const;
  bool IsAnyEngineRunning() const;
  bool AreAllEnginesRunning() const;

  // Engine commands
  void StartAll();

private:
  JSBSim::FGFDMExec &fdmExec_;
};
} // namespace sim::jsbsim
