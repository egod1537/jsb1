#include "contract/telemetry/mcap/McapTelemetrySchema.hpp"

#include "telemetry/aircraft_state.pb.h"
#include "telemetry/control.pb.h"
#include "telemetry/simulation.pb.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace telemetry::recording::mcap_schema {
namespace {
constexpr std::string_view PrimarySettingsJsonSchema = R"json({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "PrimaryRollHoldSettings",
  "type": "object",
  "additionalProperties": false,
  "required": ["roll_angle_p_gain", "roll_rate_p_gain"],
  "properties": {
    "roll_angle_p_gain": {"type": "number"},
    "roll_rate_p_gain": {"type": "number"}
  }
})json";

constexpr std::string_view BaselineSettingsJsonSchema = R"json({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "Px4RollHoldSettings",
  "type": "object",
  "additionalProperties": false,
  "required": ["fw_r_tc_sec", "fw_r_rmax_rad_per_sec", "fw_rr_p", "fw_rr_i", "fw_rr_d", "fw_rr_ff", "fw_rr_imax"],
  "properties": {
    "fw_r_tc_sec": {"type": "number", "description": "PX4 roll time constant in seconds."},
    "fw_r_rmax_rad_per_sec": {"type": "number", "description": "PX4 maximum roll rate in radians per second."},
    "fw_rr_p": {"type": "number"},
    "fw_rr_i": {"type": "number"},
    "fw_rr_d": {"type": "number"},
    "fw_rr_ff": {"type": "number"},
    "fw_rr_imax": {"type": "number"}
  }
})json";

void AddFileDescriptor(google::protobuf::FileDescriptorSet &descriptorSet,
    std::unordered_set<std::string> &files,
    const google::protobuf::FileDescriptor &file) {
  if (!files.insert(file.name()).second) {
    return;
  }
  for (int index = 0; index < file.dependency_count(); ++index) {
    AddFileDescriptor(descriptorSet, files, *file.dependency(index));
  }
  file.CopyTo(descriptorSet.add_file());
}

std::string BuildFileDescriptorSet(
    const google::protobuf::Descriptor &descriptor) {
  google::protobuf::FileDescriptorSet descriptorSet;
  std::unordered_set<std::string> files;
  AddFileDescriptor(descriptorSet, files, *descriptor.file());
  return descriptorSet.SerializeAsString();
}

bool AllFinite(const RollHoldDiagnostics &value) {
  return std::isfinite(value.commandedRollRad) && std::isfinite(value.rollRad)
         && std::isfinite(value.rollErrorRad)
         && std::isfinite(value.commandedRollRateRadPerSec)
         && std::isfinite(value.rollRateRadPerSec)
         && std::isfinite(value.rollRateErrorRadPerSec)
         && std::isfinite(value.aileronCommand);
}

std::ostringstream MakeJsonStream() {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(17);
  return stream;
}
} // namespace

const std::string &GetRollControlStateProtobufSchema() {
  static const std::string schema = BuildFileDescriptorSet(
      *jsb::telemetry::v1::RollControlState::descriptor());
  return schema;
}

const std::string &GetAircraftStateProtobufSchema() {
  static const std::string schema =
      BuildFileDescriptorSet(*jsb::telemetry::v1::AircraftState::descriptor());
  return schema;
}

const std::string &GetSimulationEventProtobufSchema() {
  static const std::string schema = BuildFileDescriptorSet(
      *jsb::telemetry::v1::SimulationEvent::descriptor());
  return schema;
}

std::string_view GetPrimarySettingsJsonSchema() {
  return PrimarySettingsJsonSchema;
}

std::string_view GetBaselineSettingsJsonSchema() {
  return BaselineSettingsJsonSchema;
}

std::optional<std::string> Serialize(const RollHoldDiagnostics &diagnostics,
    std::uint64_t simulationTimeNanoseconds) noexcept {
  if (!AllFinite(diagnostics)) {
    return std::nullopt;
  }
  try {
    jsb::telemetry::v1::RollControlState message;
    message.set_sim_time_ns(simulationTimeNanoseconds);
    message.set_commanded_roll_rad(diagnostics.commandedRollRad);
    message.set_commanded_roll_rate_rad_s(
        diagnostics.commandedRollRateRadPerSec);
    message.set_roll_error_rad(diagnostics.rollErrorRad);
    message.set_roll_rate_rad_s(diagnostics.rollRateRadPerSec);
    message.set_roll_rate_error_rad_s(diagnostics.rollRateErrorRadPerSec);
    message.set_aileron_command(diagnostics.aileronCommand);
    message.set_roll_rad(diagnostics.rollRad);
    return message.SerializeAsString();
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> Serialize(const AircraftRollState &state,
    std::uint64_t simulationTimeNanoseconds) noexcept {
  if (!std::isfinite(state.rollRad)
      || !std::isfinite(state.rollRateRadPerSec)) {
    return std::nullopt;
  }
  try {
    jsb::telemetry::v1::AircraftState message;
    message.set_sim_time_ns(simulationTimeNanoseconds);
    message.set_roll_rad(state.rollRad);
    message.set_roll_rate_rad_s(state.rollRateRadPerSec);
    return message.SerializeAsString();
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> Serialize(const ScenarioEvent &event,
    std::uint64_t simulationTimeNanoseconds) noexcept {
  if (event.type.empty()
      || (event.targetRollRad.has_value()
          && !std::isfinite(*event.targetRollRad))) {
    return std::nullopt;
  }
  try {
    jsb::telemetry::v1::SimulationEvent message;
    message.set_sim_time_ns(simulationTimeNanoseconds);
    if (event.type == "scenario_start") {
      message.set_type(jsb::telemetry::v1::SIMULATION_EVENT_STARTED);
    } else if (event.type == "scenario_end") {
      message.set_type(jsb::telemetry::v1::SIMULATION_EVENT_STOPPED);
    } else if (event.type == "roll_command_changed") {
      message.set_type(jsb::telemetry::v1::SIMULATION_EVENT_COMMAND_APPLIED);
    } else {
      message.set_type(jsb::telemetry::v1::SIMULATION_EVENT_UNSPECIFIED);
      message.set_message(event.type);
    }
    if (event.targetRollRad) {
      message.set_target_roll_rad(*event.targetRollRad);
    }
    return message.SerializeAsString();
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> Serialize(
    const PrimaryRollHoldSettings &settings) noexcept {
  if (!std::isfinite(settings.rollAngleProportionalGain)
      || !std::isfinite(settings.rollRateProportionalGain)) {
    return std::nullopt;
  }
  try {
    std::ostringstream stream = MakeJsonStream();
    stream << "{\"roll_angle_p_gain\":" << settings.rollAngleProportionalGain
           << ",\"roll_rate_p_gain\":" << settings.rollRateProportionalGain
           << '}';
    return stream.str();
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> Serialize(
    const BaselineRollHoldSettings &settings) noexcept {
  if (!std::isfinite(settings.rollTimeConstantSec)
      || !std::isfinite(settings.maximumRollRateRadPerSec)
      || !std::isfinite(settings.rateProportionalGain)
      || !std::isfinite(settings.rateIntegralGain)
      || !std::isfinite(settings.rateDerivativeGain)
      || !std::isfinite(settings.rateFeedForwardGain)
      || !std::isfinite(settings.integratorLimit)) {
    return std::nullopt;
  }
  try {
    std::ostringstream stream = MakeJsonStream();
    stream << "{\"fw_r_tc_sec\":" << settings.rollTimeConstantSec
           << ",\"fw_r_rmax_rad_per_sec\":" << settings.maximumRollRateRadPerSec
           << ",\"fw_rr_p\":" << settings.rateProportionalGain
           << ",\"fw_rr_i\":" << settings.rateIntegralGain
           << ",\"fw_rr_d\":" << settings.rateDerivativeGain
           << ",\"fw_rr_ff\":" << settings.rateFeedForwardGain
           << ",\"fw_rr_imax\":" << settings.integratorLimit << '}';
    return stream.str();
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<RollHoldDiagnostics> DeserializeRollHoldDiagnostics(
    std::string_view protobufPayload) noexcept {
  try {
    jsb::telemetry::v1::RollControlState message;
    if (!message.ParseFromArray(protobufPayload.data(),
            static_cast<int>(protobufPayload.size()))) {
      return std::nullopt;
    }
    return RollHoldDiagnostics{message.commanded_roll_rad(),
        message.roll_rad(),
        message.roll_error_rad(),
        message.commanded_roll_rate_rad_s(),
        message.roll_rate_rad_s(),
        message.roll_rate_error_rad_s(),
        message.aileron_command()};
  } catch (...) {
    return std::nullopt;
  }
}
} // namespace telemetry::recording::mcap_schema
