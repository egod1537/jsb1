#include "sim/telemetry/TelemetryRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <system_error>

namespace telemetry {
TelemetryRegistry::TelemetryRegistry(std::size_t channelCapacity)
    : channelCapacity_(channelCapacity) {}

TelemetryRegistry::~TelemetryRegistry() {
  channels_.clear();
  RemoveArchiveDirectory();
}

void TelemetryRegistry::Publish(std::string_view path, double timeSec,
    double value) {
  ++version_;
  if (std::isfinite(timeSec)) {
    if (publishedTimeRange_) {
      publishedTimeRange_->minSec =
          std::min(publishedTimeRange_->minSec, timeSec);
      publishedTimeRange_->maxSec =
          std::max(publishedTimeRange_->maxSec, timeSec);
    } else {
      publishedTimeRange_ = TelemetryTimeRange{timeSec, timeSec};
    }
  }

  auto channel = channels_.find(path);
  if (channel == channels_.end()) {
    if (archiveDirectory_.empty()) {
      CreateArchiveDirectory();
    }
    const std::filesystem::path archivePath =
        archiveDirectory_.empty()
            ? std::filesystem::path{}
            : archiveDirectory_ / (std::to_string(nextArchiveId_++) + ".bin");
    channel = channels_
                  .try_emplace(std::string(path),
                      std::string(path),
                      channelCapacity_,
                      archivePath)
                  .first;
  }

  channel->second.Push(timeSec, value);
}

const TelemetryChannel *TelemetryRegistry::Find(std::string_view path) const {
  const auto channel = channels_.find(path);
  return channel == channels_.end() ? nullptr : &channel->second;
}

std::vector<std::string_view> TelemetryRegistry::GetChannelPaths() const {
  std::vector<std::string_view> paths;
  paths.reserve(channels_.size());
  for (const auto &entry : channels_) {
    paths.push_back(entry.second.GetPath());
  }
  return paths;
}

std::optional<TelemetryTimeRange>
TelemetryRegistry::GetPublishedTimeRange() const {
  return publishedTimeRange_;
}

TelemetrySnapshot TelemetryRegistry::CaptureSnapshot() const {
  TelemetrySnapshot snapshot{
      .available = true,
      .version = version_,
      .publishedTimeRange = publishedTimeRange_,
  };
  snapshot.series.reserve(channels_.size());
  for (const auto &[path, channel] : channels_) {
    snapshot.series.push_back({
        .path = path,
        .samples = channel.CaptureSamples(),
    });
  }
  return snapshot;
}

TelemetryFrame TelemetryRegistry::CaptureLatestFrame() const {
  TelemetryFrame frame{
      .available = true,
      .sequence = version_,
      .timestamp =
          publishedTimeRange_.has_value() ? publishedTimeRange_->maxSec : 0.0,
  };
  frame.values.reserve(channels_.size());
  for (const auto &[path, channel] : channels_) {
    if (const TelemetrySample *sample = channel.GetLatest()) {
      frame.values.push_back({.path = path, .value = sample->value});
    }
  }
  return frame;
}

void TelemetryRegistry::Clear() {
  channels_.clear();
  publishedTimeRange_.reset();
  RemoveArchiveDirectory();
  ++version_;
}

void TelemetryRegistry::CreateArchiveDirectory() {
  std::error_code error;
  const std::filesystem::path temporaryDirectory =
      std::filesystem::temp_directory_path(error);
  if (error) {
    return;
  }

  const std::filesystem::path archiveRoot =
      temporaryDirectory / "jsb-flight-console-telemetry";
  std::filesystem::create_directories(archiveRoot, error);
  if (error) {
    return;
  }

  const auto timestamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::filesystem::path candidate =
        archiveRoot
        / ("session-" + std::to_string(timestamp) + "-"
            + std::to_string(attempt));
    error.clear();
    if (std::filesystem::create_directory(candidate, error)) {
      archiveDirectory_ = candidate;
      nextArchiveId_ = 0;
      return;
    }
  }
}

void TelemetryRegistry::RemoveArchiveDirectory() {
  if (archiveDirectory_.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::remove_all(archiveDirectory_, error);
  archiveDirectory_.clear();
  nextArchiveId_ = 0;
}
} // namespace telemetry
