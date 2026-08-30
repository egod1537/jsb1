#include "sim/scenario/SimulationScenario.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace {
bool ValidationFailed(sim::ScenarioValidationError *error, std::string path,
    std::string message) {
  if (error != nullptr) {
    error->path = std::move(path);
    error->message = std::move(message);
  }
  return false;
}

bool IsSupportedTrimMode(gnc::TrimMode mode) {
  switch (mode) {
  case gnc::TrimMode::Longitudinal:
  case gnc::TrimMode::Full:
  case gnc::TrimMode::Ground:
    return true;
  }
  return false;
}

bool IsCanonicalControllerParameterId(std::string_view value) {
  if (value.empty() || value.front() < 'A' || value.front() > 'Z') {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= 'A' && character <= 'Z')
           || (character >= '0' && character <= '9') || character == '_';
  });
}

} // namespace

namespace sim {
std::string ScenarioValidationError::ToString() const {
  return path.empty() ? message : path + ": " + message;
}

bool ValidateSimulationScenario(const SimulationScenario &scenario,
    ScenarioValidationError *error) {
  if (error != nullptr) {
    *error = {};
  }
  if (scenario.schemaVersion != SupportedScenarioSchemaVersion) {
    return ValidationFailed(error,
        "schema_version",
        "unsupported version " + std::to_string(scenario.schemaVersion));
  }
  if (scenario.name.empty()) {
    return ValidationFailed(error, "name", "must not be empty");
  }
  if (scenario.scenarioType != "roll_hold") {
    return ValidationFailed(error, "scenario_type", "must be roll_hold");
  }
  if (scenario.aircraft != "c172x") {
    return ValidationFailed(error,
        "aircraft",
        "unsupported aircraft '" + scenario.aircraft + "'");
  }
  for (std::size_t index = 0; index < scenario.controllerParameters.size();
       ++index) {
    const std::string &parameterId = scenario.controllerParameters[index];
    const std::string path =
        "controller_parameters[" + std::to_string(index) + "]";
    if (!IsCanonicalControllerParameterId(parameterId)) {
      return ValidationFailed(error, path, "must be a canonical parameter ID");
    }
    if (std::find(scenario.controllerParameters.begin(),
            scenario.controllerParameters.begin()
                + static_cast<std::ptrdiff_t>(index),
            parameterId)
        != scenario.controllerParameters.begin()
               + static_cast<std::ptrdiff_t>(index)) {
      return ValidationFailed(error, path, "must be unique");
    }
  }
  if (!IsSupportedTrimMode(scenario.trimMode)) {
    return ValidationFailed(error, "trim.mode", "unsupported value");
  }
  if (scenario.windEnabled) {
    return ValidationFailed(error,
        "environment.wind_enabled",
        "v1 requires false because wind state is not otherwise explicit");
  }

  const InitialCondition &initial = scenario.initialCondition;
  if (!std::isfinite(initial.latitudeDeg) || initial.latitudeDeg < -90.0
      || initial.latitudeDeg > 90.0) {
    return ValidationFailed(error,
        "initial_condition.latitude_deg",
        "must be finite and within [-90, 90]");
  }
  if (!std::isfinite(initial.longitudeDeg) || initial.longitudeDeg < -180.0
      || initial.longitudeDeg > 180.0) {
    return ValidationFailed(error,
        "initial_condition.longitude_deg",
        "must be finite and within [-180, 180]");
  }
  if (!std::isfinite(initial.altitudeFt)) {
    return ValidationFailed(error,
        "initial_condition.altitude_ft",
        "must be finite");
  }
  if (!std::isfinite(initial.airspeedKts) || initial.airspeedKts < 0.0) {
    return ValidationFailed(error,
        "initial_condition.airspeed_kts",
        "must be finite and non-negative");
  }
  const struct {
    double value;
    const char *path;
  } finiteInitialValues[] = {
      {initial.rollDeg, "initial_condition.roll_deg"},
      {initial.pitchDeg, "initial_condition.pitch_deg"},
      {initial.headingDeg, "initial_condition.heading_deg"},
      {initial.pRadPerSec, "initial_condition.p_rad_s"},
      {initial.qRadPerSec, "initial_condition.q_rad_s"},
      {initial.rRadPerSec, "initial_condition.r_rad_s"},
  };
  for (const auto &field : finiteInitialValues) {
    if (!std::isfinite(field.value)) {
      return ValidationFailed(error, field.path, "must be finite");
    }
  }
  if (!std::isfinite(scenario.durationSec) || scenario.durationSec <= 0.0) {
    return ValidationFailed(error,
        "simulation.duration_sec",
        "must be finite and greater than 0");
  }
  if (!std::isfinite(scenario.dtSec) || scenario.dtSec <= 0.0) {
    return ValidationFailed(error,
        "simulation.dt_sec",
        "must be finite and greater than 0");
  }
  const double durationSteps = scenario.durationSec / scenario.dtSec;
  if (std::abs(durationSteps - std::round(durationSteps)) > 1.0e-9) {
    return ValidationFailed(error,
        "simulation.duration_sec",
        "must be an integer multiple of simulation.dt_sec");
  }
  if (scenario.events.empty()) {
    return ValidationFailed(error, "events", "must contain at least one event");
  }
  double previousTime = -1.0;
  for (std::size_t index = 0; index < scenario.events.size(); ++index) {
    const ScenarioEventDefinition &event = scenario.events[index];
    const std::string path = "events[" + std::to_string(index) + "]";
    if (!std::isfinite(event.timeSec) || event.timeSec < 0.0) {
      return ValidationFailed(error,
          path + ".time_sec",
          "must be finite and non-negative");
    }
    if (event.timeSec < previousTime) {
      return ValidationFailed(error,
          path + ".time_sec",
          "events must be ordered by time");
    }
    if (event.timeSec >= scenario.durationSec) {
      return ValidationFailed(error,
          path + ".time_sec",
          "must be less than simulation.duration_sec");
    }
    const double eventStep = event.timeSec / scenario.dtSec;
    if (std::abs(eventStep - std::round(eventStep)) > 1.0e-9) {
      return ValidationFailed(error,
          path + ".time_sec",
          "must align to simulation.dt_sec");
    }
    if (event.command.type != ScenarioCommandType::RollHold) {
      return ValidationFailed(error,
          path + ".command.type",
          "unsupported command type");
    }
    if (!std::isfinite(event.command.rollDeg)) {
      return ValidationFailed(error,
          path + ".command.roll_deg",
          "must be finite");
    }
    previousTime = event.timeSec;
  }

  const struct {
    double value;
    const char *path;
  } nonNegativeValues[] = {
      {scenario.settlingBandDeg, "acceptance.settling_band_deg"},
      {scenario.settlingTimeLimitSec, "acceptance.settling_time_limit_sec"},
      {scenario.overshootLimitDeg, "acceptance.overshoot_limit_deg"},
      {scenario.maxOscillationCycles, "acceptance.max_oscillation_cycles"},
  };
  for (const auto &field : nonNegativeValues) {
    if (!std::isfinite(field.value) || field.value < 0.0) {
      return ValidationFailed(error,
          field.path,
          "must be finite and non-negative");
    }
  }
  return true;
}

bool ValidateSimulationScenario(const SimulationScenario &scenario,
    std::string *errorMessage) {
  ScenarioValidationError error;
  const bool valid = ValidateSimulationScenario(scenario, &error);
  if (errorMessage != nullptr) {
    *errorMessage = valid ? std::string{} : error.ToString();
  }
  return valid;
}
} // namespace sim
