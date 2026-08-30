#pragma once

#include "sim/telemetry/TelemetrySample.hpp"
#include "common/containers/RingBuffer.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace telemetry {
inline constexpr std::size_t DefaultTelemetryChannelCapacity = 4096;

class TelemetryChannel {
public:
  explicit TelemetryChannel(std::string path,
      std::size_t capacity = DefaultTelemetryChannelCapacity,
      std::filesystem::path archivePath = {});
  ~TelemetryChannel();

  TelemetryChannel(const TelemetryChannel &) = delete;
  TelemetryChannel &operator=(const TelemetryChannel &) = delete;
  TelemetryChannel(TelemetryChannel &&) noexcept = default;
  TelemetryChannel &operator=(TelemetryChannel &&) noexcept = default;

  // Identity
  std::string_view GetPath() const;

  // History
  void Push(double timeSec, double value);
  const TelemetrySample *GetLatest() const;
  const util::RingBuffer<TelemetrySample> &GetSamples() const;
  const std::vector<TelemetrySample> &ReadSamples(double minTimeSec,
      double maxTimeSec, std::size_t maxSampleCount) const;
  std::optional<TelemetrySample> FindClosestSample(double timeSec) const;
  std::size_t GetArchivedSampleCount() const;
  std::vector<TelemetrySample> CaptureSamples() const;
  void Clear();

private:
  // Archive access
  void Archive(const TelemetrySample &sample);
  void FlushPendingArchive() const;
  bool PrepareArchiveForReading() const;
  bool ReadArchiveSample(std::ifstream &stream, std::size_t index,
      TelemetrySample &sample) const;
  bool ReadCombinedSample(std::ifstream &stream, std::size_t index,
      TelemetrySample &sample) const;
  std::size_t LowerBound(std::ifstream &stream, double timeSec,
      bool upperBound) const;

  // Cached range reads
  bool CanReuseReadCache(double minTimeSec, double maxTimeSec,
      std::size_t maxSampleCount) const;
  void InvalidateReadCache();

  // Identity and in-memory history
  std::string path_;
  util::RingBuffer<TelemetrySample> samples_;

  // Disk-backed history
  std::filesystem::path archivePath_;
  mutable std::ofstream archiveStream_;
  mutable std::vector<TelemetrySample> pendingArchiveSamples_;
  mutable std::size_t archivedSampleCount_ = 0;

  // Range-read cache
  mutable std::vector<TelemetrySample> readCache_;
  mutable double readCacheMinTimeSec_ = 0.0;
  mutable double readCacheMaxTimeSec_ = 0.0;
  mutable std::size_t readCacheMaxSampleCount_ = 0;
  mutable std::size_t readCacheVersion_ = 0;
  mutable bool readCacheComplete_ = false;
  mutable bool hasReadCache_ = false;
  std::size_t version_ = 0;
};
} // namespace telemetry
