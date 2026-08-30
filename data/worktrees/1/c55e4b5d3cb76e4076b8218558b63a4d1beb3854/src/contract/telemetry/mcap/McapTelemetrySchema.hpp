#pragma once

#include "contract/telemetry/RecordingTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace telemetry::recording::mcap_schema {
inline constexpr std::string_view RollControlStateSchemaName =
    "jsb.telemetry.v1.RollControlState";
inline constexpr std::string_view AircraftStateSchemaName =
    "jsb.telemetry.v1.AircraftState";
inline constexpr std::string_view SimulationEventSchemaName =
    "jsb.telemetry.v1.SimulationEvent";
inline constexpr std::string_view PrimarySettingsSchemaName =
    "jsb_test.PrimaryRollHoldSettings";
inline constexpr std::string_view BaselineSettingsSchemaName =
    "jsb_test.Px4RollHoldSettings";

// Binary FileDescriptorSet payloads embedded in MCAP Schema records.
const std::string &GetRollControlStateProtobufSchema();
const std::string &GetAircraftStateProtobufSchema();
const std::string &GetSimulationEventProtobufSchema();

// Legacy settings/debug channels remain JSON in the first migration slice.
std::string_view GetPrimarySettingsJsonSchema();
std::string_view GetBaselineSettingsJsonSchema();

std::optional<std::string> Serialize(const RollHoldDiagnostics &diagnostics,
    std::uint64_t simulationTimeNanoseconds) noexcept;
std::optional<std::string> Serialize(const AircraftRollState &state,
    std::uint64_t simulationTimeNanoseconds) noexcept;
std::optional<std::string> Serialize(const ScenarioEvent &event,
    std::uint64_t simulationTimeNanoseconds) noexcept;
std::optional<std::string> Serialize(
    const PrimaryRollHoldSettings &settings) noexcept;
std::optional<std::string> Serialize(
    const BaselineRollHoldSettings &settings) noexcept;
std::optional<RollHoldDiagnostics> DeserializeRollHoldDiagnostics(
    std::string_view protobufPayload) noexcept;
} // namespace telemetry::recording::mcap_schema
