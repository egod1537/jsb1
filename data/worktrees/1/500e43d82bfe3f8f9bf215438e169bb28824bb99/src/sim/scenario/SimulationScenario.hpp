#pragma once

#include "sim/InitialCondition.hpp"
#include "sim/gnc/TrimTypes.hpp"

#include <string>
#include <vector>

namespace sim {
inline constexpr int SupportedScenarioSchemaVersion = 1;

enum class ScenarioCommandType { RollHold };

struct ScenarioCommand {
  ScenarioCommandType type = ScenarioCommandType::RollHold;
  double rollDeg = 5.0;
  bool operator==(const ScenarioCommand &) const = default;
};

struct ScenarioEventDefinition {
  double timeSec = 5.0;
  ScenarioCommand command;
  bool operator==(const ScenarioEventDefinition &) const = default;
};

struct SimulationScenario {
  // Contract identity
  int schemaVersion = SupportedScenarioSchemaVersion;
  std::string scenarioType = "roll_hold";
  std::string name = "Roll Hold 5deg 30s";
  std::string aircraft = "c172x";

  // Experiment conditions
  InitialCondition initialCondition{
      .latitudeDeg = 0.0,
      .longitudeDeg = 0.0,
      .altitudeFt = 3000.0,
      .rollDeg = 0.0,
      .pitchDeg = 0.0,
      .headingDeg = 0.0,
      .airspeedKts = 100.0,
      .pRadPerSec = 0.0,
      .qRadPerSec = 0.0,
      .rRadPerSec = 0.0,
  };
  bool windEnabled = false;
  bool runTrim = true;
  gnc::TrimMode trimMode = gnc::TrimMode::Full;
  double durationSec = 30.0;
  double dtSec = 1.0 / 30.0;
  std::vector<ScenarioEventDefinition> events{{}};

  // Analysis acceptance criteria, not runtime controller behavior
  double settlingBandDeg = 0.5;
  double settlingTimeLimitSec = 10.0;
  double overshootLimitDeg = 1.0;
  double maxOscillationCycles = 2.0;

  bool operator==(const SimulationScenario &) const = default;
};

struct ScenarioValidationError {
  std::string path;
  std::string message;
  std::string ToString() const;
};

bool ValidateSimulationScenario(const SimulationScenario &scenario,
    ScenarioValidationError *error);
bool ValidateSimulationScenario(const SimulationScenario &scenario,
    std::string *errorMessage = nullptr);
} // namespace sim
