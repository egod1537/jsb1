#include "sim/scenario/SimulationScenarioSerializer.hpp"
#include "common/crypto/Sha256.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace sim {
namespace {
const char *TrimModeName(gnc::TrimMode mode) {
  switch (mode) {
  case gnc::TrimMode::Longitudinal:
    return "Longitudinal";
  case gnc::TrimMode::Full:
    return "Full";
  case gnc::TrimMode::Ground:
    return "Ground";
  }
  return "Unknown";
}

gnc::TrimMode ParseTrimMode(const std::string &value) {
  if (value == "Longitudinal")
    return gnc::TrimMode::Longitudinal;
  if (value == "Full")
    return gnc::TrimMode::Full;
  if (value == "Ground")
    return gnc::TrimMode::Ground;
  throw std::runtime_error("trim.mode must be Longitudinal, Full, or Ground");
}

void RequireOnlyKeys(const YAML::Node &node,
    std::initializer_list<std::string_view> allowed, std::string_view path) {
  for (const auto &entry : node) {
    const std::string key = entry.first.as<std::string>();
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
      throw std::runtime_error(
          std::string(path) + " has unexpected field: " + key);
    }
  }
}

YAML::Node RequireMap(const YAML::Node &parent, const char *key,
    const std::string &path) {
  const YAML::Node node = parent[key];
  if (!node)
    throw std::runtime_error("missing required field: " + path);
  if (!node.IsMap())
    throw std::runtime_error(path + " must be a mapping");
  return node;
}

YAML::Node RequireSequence(const YAML::Node &parent, const char *key,
    const std::string &path) {
  const YAML::Node node = parent[key];
  if (!node)
    throw std::runtime_error("missing required field: " + path);
  if (!node.IsSequence())
    throw std::runtime_error(path + " must be a sequence");
  return node;
}

template <typename T>
T ReadRequired(const YAML::Node &parent, const char *key,
    const std::string &path) {
  const YAML::Node node = parent[key];
  if (!node)
    throw std::runtime_error("missing required field: " + path);
  if (!node.IsScalar())
    throw std::runtime_error(path + " must be a scalar value");
  try {
    return node.as<T>();
  } catch (const YAML::Exception &exception) {
    throw std::runtime_error(path + " has an invalid type: " + exception.msg);
  }
}

void Validate(const SimulationScenario &scenario) {
  ScenarioValidationError error;
  if (!ValidateSimulationScenario(scenario, &error)) {
    throw std::runtime_error(error.ToString());
  }
}

SimulationScenario ParseScenario(const YAML::Node &root,
    ScenarioLoadMetadata &metadata) {
  if (!root || !root.IsMap()) {
    throw std::runtime_error("scenario YAML root must be a mapping");
  }
  RequireOnlyKeys(root,
      {"schema_version",
          "scenario_type",
          "name",
          "aircraft",
          "autopilot",
          "initial_condition",
          "environment",
          "trim",
          "events",
          "simulation",
          "acceptance"},
      "scenario");

  SimulationScenario scenario;
  scenario.schemaVersion =
      ReadRequired<int>(root, "schema_version", "schema_version");
  scenario.scenarioType =
      ReadRequired<std::string>(root, "scenario_type", "scenario_type");
  scenario.name = ReadRequired<std::string>(root, "name", "name");
  scenario.aircraft = ReadRequired<std::string>(root, "aircraft", "aircraft");

  if (const YAML::Node autopilot = root["autopilot"]) {
    std::string legacyValue;
    if (autopilot.IsScalar()) {
      legacyValue = autopilot.as<std::string>();
    } else if (autopilot.IsMap()) {
      RequireOnlyKeys(autopilot, {"type"}, "autopilot");
      legacyValue =
          ReadRequired<std::string>(autopilot, "type", "autopilot.type");
    } else {
      throw std::runtime_error("autopilot must be a string or mapping");
    }
    ExecutionVariant legacyVariant;
    if (!TryParseExecutionVariant(legacyValue, legacyVariant)) {
      throw std::runtime_error(
          "autopilot: unsupported legacy value '" + legacyValue + "'");
    }
    metadata.legacyVariant = legacyVariant;
    metadata.warnings.push_back(
        "Deprecated scenario field 'autopilot' (" + legacyValue
        + "). Execution variants are not Scenario inputs.");
  }

  const YAML::Node initial =
      RequireMap(root, "initial_condition", "initial_condition");
  RequireOnlyKeys(initial,
      {"latitude_deg",
          "longitude_deg",
          "altitude_ft",
          "airspeed_kts",
          "roll_deg",
          "pitch_deg",
          "heading_deg",
          "p_rad_s",
          "q_rad_s",
          "r_rad_s"},
      "initial_condition");
  scenario.initialCondition.latitudeDeg = ReadRequired<double>(initial,
      "latitude_deg",
      "initial_condition.latitude_deg");
  scenario.initialCondition.longitudeDeg = ReadRequired<double>(initial,
      "longitude_deg",
      "initial_condition.longitude_deg");
  scenario.initialCondition.altitudeFt = ReadRequired<double>(initial,
      "altitude_ft",
      "initial_condition.altitude_ft");
  scenario.initialCondition.airspeedKts = ReadRequired<double>(initial,
      "airspeed_kts",
      "initial_condition.airspeed_kts");
  scenario.initialCondition.rollDeg =
      ReadRequired<double>(initial, "roll_deg", "initial_condition.roll_deg");
  scenario.initialCondition.pitchDeg =
      ReadRequired<double>(initial, "pitch_deg", "initial_condition.pitch_deg");
  scenario.initialCondition.headingDeg = ReadRequired<double>(initial,
      "heading_deg",
      "initial_condition.heading_deg");
  scenario.initialCondition.pRadPerSec =
      ReadRequired<double>(initial, "p_rad_s", "initial_condition.p_rad_s");
  scenario.initialCondition.qRadPerSec =
      ReadRequired<double>(initial, "q_rad_s", "initial_condition.q_rad_s");
  scenario.initialCondition.rRadPerSec =
      ReadRequired<double>(initial, "r_rad_s", "initial_condition.r_rad_s");

  const YAML::Node environment = RequireMap(root, "environment", "environment");
  RequireOnlyKeys(environment, {"wind_enabled"}, "environment");
  scenario.windEnabled = ReadRequired<bool>(environment,
      "wind_enabled",
      "environment.wind_enabled");

  const YAML::Node trim = RequireMap(root, "trim", "trim");
  RequireOnlyKeys(trim, {"enabled", "mode"}, "trim");
  scenario.runTrim = ReadRequired<bool>(trim, "enabled", "trim.enabled");
  scenario.trimMode =
      ParseTrimMode(ReadRequired<std::string>(trim, "mode", "trim.mode"));

  const YAML::Node simulation = RequireMap(root, "simulation", "simulation");
  RequireOnlyKeys(simulation, {"duration_sec", "dt_sec"}, "simulation");
  scenario.durationSec = ReadRequired<double>(simulation,
      "duration_sec",
      "simulation.duration_sec");
  scenario.dtSec =
      ReadRequired<double>(simulation, "dt_sec", "simulation.dt_sec");

  const YAML::Node events = RequireSequence(root, "events", "events");
  scenario.events.clear();
  for (std::size_t index = 0; index < events.size(); ++index) {
    const YAML::Node event = events[index];
    const std::string path = "events[" + std::to_string(index) + "]";
    if (!event.IsMap())
      throw std::runtime_error(path + " must be a mapping");
    RequireOnlyKeys(event, {"time_sec", "command"}, path);
    ScenarioEventDefinition definition;
    definition.timeSec =
        ReadRequired<double>(event, "time_sec", path + ".time_sec");
    const YAML::Node command = RequireMap(event, "command", path + ".command");
    RequireOnlyKeys(command, {"type", "roll_deg"}, path + ".command");
    const std::string type =
        ReadRequired<std::string>(command, "type", path + ".command.type");
    if (type != "roll_hold") {
      throw std::runtime_error(
          path + ".command.type: unsupported command '" + type + "'");
    }
    definition.command.type = ScenarioCommandType::RollHold;
    definition.command.rollDeg =
        ReadRequired<double>(command, "roll_deg", path + ".command.roll_deg");
    scenario.events.push_back(definition);
  }

  const YAML::Node acceptance = RequireMap(root, "acceptance", "acceptance");
  RequireOnlyKeys(acceptance,
      {"settling_band_deg",
          "settling_time_limit_sec",
          "overshoot_limit_deg",
          "max_oscillation_cycles"},
      "acceptance");
  scenario.settlingBandDeg = ReadRequired<double>(acceptance,
      "settling_band_deg",
      "acceptance.settling_band_deg");
  scenario.settlingTimeLimitSec = ReadRequired<double>(acceptance,
      "settling_time_limit_sec",
      "acceptance.settling_time_limit_sec");
  scenario.overshootLimitDeg = ReadRequired<double>(acceptance,
      "overshoot_limit_deg",
      "acceptance.overshoot_limit_deg");
  scenario.maxOscillationCycles = ReadRequired<double>(acceptance,
      "max_oscillation_cycles",
      "acceptance.max_oscillation_cycles");

  Validate(scenario);
  return scenario;
}
} // namespace

std::string SimulationScenarioSerializer::Serialize(
    const SimulationScenario &scenario) {
  Validate(scenario);
  YAML::Emitter output;
  output << YAML::BeginMap;
  output << YAML::Key << "schema_version" << YAML::Value
         << scenario.schemaVersion;
  output << YAML::Key << "scenario_type" << YAML::Value
         << scenario.scenarioType;
  output << YAML::Key << "name" << YAML::Value << scenario.name;
  output << YAML::Key << "aircraft" << YAML::Value << scenario.aircraft;
  const InitialCondition &initial = scenario.initialCondition;
  output << YAML::Key << "initial_condition" << YAML::Value << YAML::BeginMap
         << YAML::Key << "latitude_deg" << YAML::Value << initial.latitudeDeg
         << YAML::Key << "longitude_deg" << YAML::Value << initial.longitudeDeg
         << YAML::Key << "altitude_ft" << YAML::Value << initial.altitudeFt
         << YAML::Key << "airspeed_kts" << YAML::Value << initial.airspeedKts
         << YAML::Key << "roll_deg" << YAML::Value << initial.rollDeg
         << YAML::Key << "pitch_deg" << YAML::Value << initial.pitchDeg
         << YAML::Key << "heading_deg" << YAML::Value << initial.headingDeg
         << YAML::Key << "p_rad_s" << YAML::Value << initial.pRadPerSec
         << YAML::Key << "q_rad_s" << YAML::Value << initial.qRadPerSec
         << YAML::Key << "r_rad_s" << YAML::Value << initial.rRadPerSec
         << YAML::EndMap;
  output << YAML::Key << "environment" << YAML::Value << YAML::BeginMap
         << YAML::Key << "wind_enabled" << YAML::Value << scenario.windEnabled
         << YAML::EndMap;
  output << YAML::Key << "trim" << YAML::Value << YAML::BeginMap << YAML::Key
         << "enabled" << YAML::Value << scenario.runTrim << YAML::Key << "mode"
         << YAML::Value << TrimModeName(scenario.trimMode) << YAML::EndMap;
  output << YAML::Key << "simulation" << YAML::Value << YAML::BeginMap
         << YAML::Key << "duration_sec" << YAML::Value << scenario.durationSec
         << YAML::Key << "dt_sec" << YAML::Value << scenario.dtSec
         << YAML::EndMap;
  output << YAML::Key << "events" << YAML::Value << YAML::BeginSeq;
  for (const ScenarioEventDefinition &event : scenario.events) {
    output << YAML::BeginMap << YAML::Key << "time_sec" << YAML::Value
           << event.timeSec << YAML::Key << "command" << YAML::Value
           << YAML::BeginMap << YAML::Key << "type" << YAML::Value
           << "roll_hold" << YAML::Key << "roll_deg" << YAML::Value
           << event.command.rollDeg << YAML::EndMap << YAML::EndMap;
  }
  output << YAML::EndSeq;
  output << YAML::Key << "acceptance" << YAML::Value << YAML::BeginMap
         << YAML::Key << "settling_band_deg" << YAML::Value
         << scenario.settlingBandDeg << YAML::Key << "settling_time_limit_sec"
         << YAML::Value << scenario.settlingTimeLimitSec << YAML::Key
         << "overshoot_limit_deg" << YAML::Value << scenario.overshootLimitDeg
         << YAML::Key << "max_oscillation_cycles" << YAML::Value
         << scenario.maxOscillationCycles << YAML::EndMap << YAML::EndMap;
  if (!output.good())
    throw std::runtime_error(output.GetLastError());
  return std::string(output.c_str()) + '\n';
}

bool SimulationScenarioSerializer::Deserialize(std::string_view yaml,
    SimulationScenario &scenario, std::string &error,
    ScenarioLoadMetadata *metadata) {
  try {
    ScenarioLoadMetadata parsedMetadata;
    SimulationScenario parsed =
        ParseScenario(YAML::Load(std::string(yaml)), parsedMetadata);
    scenario = std::move(parsed);
    if (metadata != nullptr) {
      *metadata = std::move(parsedMetadata);
    }
    error.clear();
    return true;
  } catch (const YAML::Exception &exception) {
    error = "YAML parse error: " + exception.msg;
  } catch (const std::exception &exception) {
    error = "ScenarioValidationError: " + std::string(exception.what());
  }
  return false;
}

bool SimulationScenarioSerializer::Load(const std::filesystem::path &path,
    SimulationScenario &scenario, std::string &error,
    ScenarioLoadMetadata *metadata) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open scenario file: " + path.string();
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad()) {
    error = "Could not read scenario file: " + path.string();
    return false;
  }
  const std::string bytes = buffer.str();
  ScenarioLoadMetadata parsedMetadata;
  if (!Deserialize(bytes, scenario, error, &parsedMetadata)) {
    return false;
  }
  parsedMetadata.sourceFile = path.string();
  parsedMetadata.sourceDigestSha256 = common::crypto::Sha256Hex(bytes);
  if (metadata != nullptr) {
    *metadata = std::move(parsedMetadata);
  }
  return true;
}

bool SimulationScenarioSerializer::Save(const std::filesystem::path &path,
    const SimulationScenario &scenario, std::string &error) {
  try {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
      std::error_code filesystemError;
      std::filesystem::create_directories(parent, filesystemError);
      if (filesystemError) {
        error =
            "Could not create scenario directory: " + filesystemError.message();
        return false;
      }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "Could not open scenario file for writing: " + path.string();
      return false;
    }
    output << Serialize(scenario);
    if (!output) {
      error = "Could not write scenario file: " + path.string();
      return false;
    }
    error.clear();
    return true;
  } catch (const std::exception &exception) {
    error = "Could not save scenario: " + std::string(exception.what());
    return false;
  }
}
} // namespace sim
