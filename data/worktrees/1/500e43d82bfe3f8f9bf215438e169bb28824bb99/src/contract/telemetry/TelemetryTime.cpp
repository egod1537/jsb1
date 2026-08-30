#include "contract/telemetry/TelemetryTime.hpp"

#include <cmath>
#include <limits>

namespace telemetry::recording {
std::optional<std::uint64_t> SimulationTimeToNanoseconds(
    double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds < 0.0) {
    return std::nullopt;
  }

  constexpr long double NanosecondsPerSecond = 1'000'000'000.0L;
  const long double nanoseconds =
      std::round(static_cast<long double>(seconds) * NanosecondsPerSecond);
  if (nanoseconds < 0.0L
      || nanoseconds > static_cast<long double>(
             std::numeric_limits<std::uint64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(nanoseconds);
}

double NanosecondsToSimulationTime(std::uint64_t nanoseconds) noexcept {
  return static_cast<double>(nanoseconds) / 1'000'000'000.0;
}
} // namespace telemetry::recording
