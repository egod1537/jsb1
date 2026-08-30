#include "sim/jsbsim/EngineSystem.hpp"

#include <FGFDMExec.h>
#include <algorithm>
#include <models/FGFCS.h>
#include <models/FGPropulsion.h>
#include <models/propulsion/FGEngine.h>
#include <models/propulsion/FGThruster.h>

namespace sim::jsbsim {
namespace {
constexpr const char *SetAllEnginesRunning = "propulsion/set-running";
}

EngineSystem::EngineSystem(JSBSim::FGFDMExec &fdmExec) : fdmExec_(fdmExec) {}

std::size_t EngineSystem::GetEngineCount() const {
  const auto propulsion = fdmExec_.GetPropulsion();
  return propulsion != nullptr ? propulsion->GetNumEngines() : 0U;
}

EngineState EngineSystem::GetEngineState(std::size_t index) const {
  EngineState state{};
  state.index = index;

  const auto propulsion = fdmExec_.GetPropulsion();
  const auto fcs = fdmExec_.GetFCS();
  if (propulsion == nullptr || index >= propulsion->GetNumEngines()) {
    return state;
  }

  const auto engine = propulsion->GetEngine(static_cast<unsigned int>(index));
  if (engine == nullptr) {
    return state;
  }

  state.running = engine->GetRunning();
  state.throttleCommand =
      fcs != nullptr ? fcs->GetThrottleCmd(static_cast<int>(index)) : 0.0;

  const auto thruster = engine->GetThruster();
  state.rpm = thruster != nullptr ? thruster->GetEngineRPM() : 0.0;

  return state;
}

std::vector<EngineState> EngineSystem::GetEngineStates() const {
  std::vector<EngineState> states;
  const std::size_t engineCount = GetEngineCount();
  states.reserve(engineCount);

  for (std::size_t index = 0; index < engineCount; ++index) {
    states.push_back(GetEngineState(index));
  }

  return states;
}

bool EngineSystem::IsAnyEngineRunning() const {
  for (const EngineState &engineState : GetEngineStates()) {
    if (engineState.running) {
      return true;
    }
  }

  return false;
}

bool EngineSystem::AreAllEnginesRunning() const {
  const auto engineStates = GetEngineStates();
  if (engineStates.empty()) {
    return false;
  }

  return std::all_of(engineStates.begin(),
      engineStates.end(),
      [](const EngineState &engineState) { return engineState.running; });
}

void EngineSystem::StartAll() {
  fdmExec_.SetPropertyValue(SetAllEnginesRunning, -1.0);
}
} // namespace sim::jsbsim
