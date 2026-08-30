#pragma once

#include <cstdint>
#include <optional>

namespace telemetry::recording {
std::optional<std::uint64_t> SimulationTimeToNanoseconds(
    double seconds) noexcept;
double NanosecondsToSimulationTime(std::uint64_t nanoseconds) noexcept;
} // namespace telemetry::recording
