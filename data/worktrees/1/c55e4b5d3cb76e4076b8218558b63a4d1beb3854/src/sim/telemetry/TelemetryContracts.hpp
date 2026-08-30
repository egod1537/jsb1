#pragma once

#include "sim/telemetry/TelemetrySample.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace telemetry {
struct TelemetryTimeRange {
  double minSec = 0.0;
  double maxSec = 0.0;
};

struct TelemetrySeries {
  std::string path;
  std::vector<TelemetrySample> samples;
};

struct TelemetryValue {
  std::string path;
  double value = 0.0;
};

struct TelemetryFrame {
  bool available = false;
  std::uint64_t sequence = 0;
  double timestamp = 0.0;
  std::vector<TelemetryValue> values;
};

struct TelemetrySnapshot {
  bool available = false;
  std::uint64_t version = 0;
  std::optional<TelemetryTimeRange> publishedTimeRange;
  std::vector<TelemetrySeries> series;

  const TelemetrySeries *Find(std::string_view path) const;
  std::vector<std::string_view> GetChannelPaths() const;
};

std::vector<TelemetrySample> ReadTelemetrySamples(const TelemetrySeries &series,
    double minTimeSec, double maxTimeSec, std::size_t maxSampleCount);
std::optional<TelemetrySample> FindClosestTelemetrySample(
    const TelemetrySeries &series, double timeSec);
} // namespace telemetry
