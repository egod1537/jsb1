#include "sim/telemetry/TelemetryContracts.hpp"

#include <algorithm>
#include <cmath>

namespace telemetry {
const TelemetrySeries *TelemetrySnapshot::Find(std::string_view path) const {
  const auto found = std::lower_bound(series.begin(),
      series.end(),
      path,
      [](const TelemetrySeries &candidate, std::string_view requested) {
        return candidate.path < requested;
      });
  return found != series.end() && found->path == path ? &*found : nullptr;
}

std::vector<std::string_view> TelemetrySnapshot::GetChannelPaths() const {
  std::vector<std::string_view> paths;
  paths.reserve(series.size());
  for (const TelemetrySeries &channel : series) {
    paths.push_back(channel.path);
  }
  return paths;
}

std::vector<TelemetrySample> ReadTelemetrySamples(const TelemetrySeries &series,
    double minTimeSec, double maxTimeSec, std::size_t maxSampleCount) {
  std::vector<TelemetrySample> result;
  if (maxSampleCount == 0 || !std::isfinite(minTimeSec)
      || !std::isfinite(maxTimeSec) || minTimeSec > maxTimeSec) {
    return result;
  }

  const auto first = std::lower_bound(series.samples.begin(),
      series.samples.end(),
      minTimeSec,
      [](const TelemetrySample &sample, double timeSec) {
        return sample.timeSec < timeSec;
      });
  const auto last = std::upper_bound(first,
      series.samples.end(),
      maxTimeSec,
      [](double timeSec, const TelemetrySample &sample) {
        return timeSec < sample.timeSec;
      });
  const std::size_t available =
      static_cast<std::size_t>(std::distance(first, last));
  const std::size_t outputCount = std::min(available, maxSampleCount);
  result.reserve(outputCount);
  if (outputCount == 0) {
    return result;
  }
  if (outputCount == available) {
    result.assign(first, last);
    return result;
  }
  if (outputCount == 1) {
    result.push_back(*first);
    return result;
  }

  for (std::size_t index = 0; index < outputCount; ++index) {
    const long double ratio = static_cast<long double>(index)
                              / static_cast<long double>(outputCount - 1);
    const std::size_t offset = static_cast<std::size_t>(
        std::llround(ratio * static_cast<long double>(available - 1)));
    result.push_back(*(first + static_cast<std::ptrdiff_t>(offset)));
  }
  return result;
}

std::optional<TelemetrySample> FindClosestTelemetrySample(
    const TelemetrySeries &series, double timeSec) {
  if (!std::isfinite(timeSec) || series.samples.empty()) {
    return std::nullopt;
  }
  const auto after = std::lower_bound(series.samples.begin(),
      series.samples.end(),
      timeSec,
      [](const TelemetrySample &sample, double requested) {
        return sample.timeSec < requested;
      });
  if (after == series.samples.begin()) {
    return *after;
  }
  if (after == series.samples.end()) {
    return series.samples.back();
  }
  const TelemetrySample &before = *(after - 1);
  return timeSec - before.timeSec <= after->timeSec - timeSec ? before : *after;
}
} // namespace telemetry
