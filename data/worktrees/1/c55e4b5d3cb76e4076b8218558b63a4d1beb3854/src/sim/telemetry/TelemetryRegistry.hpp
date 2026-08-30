#pragma once

#include "sim/telemetry/TelemetryChannel.hpp"
#include "sim/telemetry/TelemetryContracts.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace telemetry {
class TelemetryRegistry {
public:
  explicit TelemetryRegistry(
      std::size_t channelCapacity = DefaultTelemetryChannelCapacity);
  ~TelemetryRegistry();

  TelemetryRegistry(const TelemetryRegistry &) = delete;
  TelemetryRegistry &operator=(const TelemetryRegistry &) = delete;

  // Publishing and lookup
  void Publish(std::string_view path, double timeSec, double value);
  const TelemetryChannel *Find(std::string_view path) const;

  // Registry inspection
  std::vector<std::string_view> GetChannelPaths() const;
  std::optional<TelemetryTimeRange> GetPublishedTimeRange() const;
  std::uint64_t GetVersion() const { return version_; }
  TelemetryFrame CaptureLatestFrame() const;
  TelemetrySnapshot CaptureSnapshot() const;
  void Clear();

private:
  // Archive lifecycle
  void CreateArchiveDirectory();
  void RemoveArchiveDirectory();

  // Channel storage
  std::map<std::string, TelemetryChannel, std::less<>> channels_;
  std::size_t channelCapacity_;
  std::filesystem::path archiveDirectory_;
  std::size_t nextArchiveId_ = 0;

  // Session bounds
  std::optional<TelemetryTimeRange> publishedTimeRange_;
  std::uint64_t version_ = 0;
};
} // namespace telemetry
