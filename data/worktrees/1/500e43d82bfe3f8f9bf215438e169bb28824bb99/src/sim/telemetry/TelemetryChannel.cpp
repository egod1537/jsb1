#include "sim/telemetry/TelemetryChannel.hpp"

#include <algorithm>
#include <cmath>
#include <ios>
#include <limits>
#include <type_traits>
#include <utility>

namespace telemetry {
namespace {
constexpr std::size_t ArchiveBatchSampleCount = 256;
static_assert(std::is_trivially_copyable_v<TelemetrySample>);
} // namespace

TelemetryChannel::TelemetryChannel(std::string path, std::size_t capacity,
    std::filesystem::path archivePath)
    : path_(std::move(path)), samples_(capacity),
      archivePath_(std::move(archivePath)) {
  if (!archivePath_.empty()) {
    archiveStream_.open(archivePath_,
        std::ios::binary | std::ios::out | std::ios::trunc);
    pendingArchiveSamples_.reserve(ArchiveBatchSampleCount);
  }
}

TelemetryChannel::~TelemetryChannel() {
  FlushPendingArchive();
  archiveStream_.flush();
}

std::string_view TelemetryChannel::GetPath() const { return path_; }

void TelemetryChannel::Push(double timeSec, double value) {
  if (samples_.IsFull()) {
    Archive(samples_.Front());
  }
  samples_.Push(TelemetrySample{timeSec, value});
  ++version_;
}

const TelemetrySample *TelemetryChannel::GetLatest() const {
  return samples_.IsEmpty() ? nullptr : &samples_.Back();
}

const util::RingBuffer<TelemetrySample> &TelemetryChannel::GetSamples() const {
  return samples_;
}

const std::vector<TelemetrySample> &TelemetryChannel::ReadSamples(
    double minTimeSec, double maxTimeSec, std::size_t maxSampleCount) const {
  if (CanReuseReadCache(minTimeSec, maxTimeSec, maxSampleCount)) {
    return readCache_;
  }

  readCache_.clear();
  readCacheMinTimeSec_ = minTimeSec;
  readCacheMaxTimeSec_ = maxTimeSec;
  readCacheMaxSampleCount_ = maxSampleCount;
  readCacheVersion_ = version_;
  hasReadCache_ = true;

  const TelemetrySample *latest = GetLatest();
  readCacheComplete_ = latest != nullptr && latest->timeSec > maxTimeSec;
  if (maxSampleCount == 0 || !std::isfinite(minTimeSec)
      || !std::isfinite(maxTimeSec) || minTimeSec > maxTimeSec
      || (archivedSampleCount_ == 0 && samples_.IsEmpty())) {
    return readCache_;
  }

  PrepareArchiveForReading();
  std::ifstream archiveInput;
  if (archivedSampleCount_ > 0) {
    archiveInput.open(archivePath_, std::ios::binary | std::ios::in);
    if (!archiveInput) {
      return readCache_;
    }
  }

  const std::size_t firstIndex = LowerBound(archiveInput, minTimeSec, false);
  const std::size_t endIndex = LowerBound(archiveInput, maxTimeSec, true);
  if (firstIndex >= endIndex) {
    return readCache_;
  }

  const std::size_t availableCount = endIndex - firstIndex;
  const std::size_t outputCount = std::min(availableCount, maxSampleCount);
  readCache_.reserve(outputCount);

  if (outputCount == 1) {
    TelemetrySample sample;
    if (ReadCombinedSample(archiveInput, firstIndex, sample)) {
      readCache_.push_back(sample);
    }
    return readCache_;
  }

  for (std::size_t outputIndex = 0; outputIndex < outputCount; ++outputIndex) {
    const long double ratio = static_cast<long double>(outputIndex)
                              / static_cast<long double>(outputCount - 1);
    const std::size_t sourceOffset = static_cast<std::size_t>(
        std::llround(ratio * static_cast<long double>(availableCount - 1)));
    TelemetrySample sample;
    if (ReadCombinedSample(archiveInput, firstIndex + sourceOffset, sample)) {
      readCache_.push_back(sample);
    }
  }
  return readCache_;
}

std::optional<TelemetrySample> TelemetryChannel::FindClosestSample(
    double timeSec) const {
  if (!std::isfinite(timeSec)
      || (archivedSampleCount_ == 0 && samples_.IsEmpty())) {
    return std::nullopt;
  }

  PrepareArchiveForReading();
  std::ifstream archiveInput;
  if (archivedSampleCount_ > 0) {
    archiveInput.open(archivePath_, std::ios::binary | std::ios::in);
    if (!archiveInput) {
      return std::nullopt;
    }
  }

  const std::size_t sampleCount = archivedSampleCount_ + samples_.GetSize();
  const std::size_t afterIndex = LowerBound(archiveInput, timeSec, false);
  TelemetrySample sample;
  if (afterIndex == 0) {
    return ReadCombinedSample(archiveInput, 0, sample)
               ? std::optional<TelemetrySample>(sample)
               : std::nullopt;
  }
  if (afterIndex == sampleCount) {
    return ReadCombinedSample(archiveInput, sampleCount - 1, sample)
               ? std::optional<TelemetrySample>(sample)
               : std::nullopt;
  }

  TelemetrySample before;
  TelemetrySample after;
  if (!ReadCombinedSample(archiveInput, afterIndex - 1, before)
      || !ReadCombinedSample(archiveInput, afterIndex, after)) {
    return std::nullopt;
  }
  return timeSec - before.timeSec <= after.timeSec - timeSec ? before : after;
}

std::size_t TelemetryChannel::GetArchivedSampleCount() const {
  return archivedSampleCount_;
}

std::vector<TelemetrySample> TelemetryChannel::CaptureSamples() const {
  const std::size_t sampleCount = archivedSampleCount_ + samples_.GetSize();
  const std::vector<TelemetrySample> &samples =
      ReadSamples(std::numeric_limits<double>::lowest(),
          std::numeric_limits<double>::max(),
          sampleCount);
  return samples;
}

void TelemetryChannel::Clear() {
  samples_.Clear();
  pendingArchiveSamples_.clear();
  archivedSampleCount_ = 0;
  ++version_;
  InvalidateReadCache();

  if (archiveStream_.is_open()) {
    archiveStream_.close();
  }
  if (!archivePath_.empty()) {
    archiveStream_.open(archivePath_,
        std::ios::binary | std::ios::out | std::ios::trunc);
  }
}

void TelemetryChannel::Archive(const TelemetrySample &sample) {
  if (!archiveStream_) {
    return;
  }
  pendingArchiveSamples_.push_back(sample);
  ++archivedSampleCount_;
  if (pendingArchiveSamples_.size() >= ArchiveBatchSampleCount) {
    FlushPendingArchive();
  }
}

void TelemetryChannel::FlushPendingArchive() const {
  if (pendingArchiveSamples_.empty()) {
    return;
  }

  const std::size_t pendingSampleCount = pendingArchiveSamples_.size();
  archiveStream_.write(
      reinterpret_cast<const char *>(pendingArchiveSamples_.data()),
      static_cast<std::streamsize>(
          pendingSampleCount * sizeof(TelemetrySample)));
  if (!archiveStream_) {
    archivedSampleCount_ -= pendingSampleCount;
  }
  pendingArchiveSamples_.clear();
}

bool TelemetryChannel::PrepareArchiveForReading() const {
  if (archivedSampleCount_ == 0) {
    return true;
  }
  FlushPendingArchive();
  archiveStream_.flush();
  return static_cast<bool>(archiveStream_);
}

bool TelemetryChannel::ReadArchiveSample(std::ifstream &stream,
    std::size_t index, TelemetrySample &sample) const {
  if (!stream || index >= archivedSampleCount_) {
    return false;
  }
  const auto byteOffset =
      static_cast<std::streamoff>(index * sizeof(TelemetrySample));
  stream.clear();
  stream.seekg(byteOffset, std::ios::beg);
  stream.read(reinterpret_cast<char *>(&sample), sizeof(TelemetrySample));
  return static_cast<bool>(stream);
}

bool TelemetryChannel::ReadCombinedSample(std::ifstream &stream,
    std::size_t index, TelemetrySample &sample) const {
  if (index < archivedSampleCount_) {
    return ReadArchiveSample(stream, index, sample);
  }
  const std::size_t memoryIndex = index - archivedSampleCount_;
  if (memoryIndex >= samples_.GetSize()) {
    return false;
  }
  sample = samples_[memoryIndex];
  return true;
}

std::size_t TelemetryChannel::LowerBound(std::ifstream &stream, double timeSec,
    bool upperBound) const {
  std::size_t first = 0;
  std::size_t last = archivedSampleCount_ + samples_.GetSize();
  while (first < last) {
    const std::size_t middle = first + (last - first) / 2;
    TelemetrySample sample;
    if (!ReadCombinedSample(stream, middle, sample)) {
      return first;
    }
    const bool advance =
        upperBound ? sample.timeSec <= timeSec : sample.timeSec < timeSec;
    if (advance) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  return first;
}

bool TelemetryChannel::CanReuseReadCache(double minTimeSec, double maxTimeSec,
    std::size_t maxSampleCount) const {
  if (!hasReadCache_ || readCacheMinTimeSec_ != minTimeSec
      || readCacheMaxTimeSec_ != maxTimeSec
      || readCacheMaxSampleCount_ != maxSampleCount) {
    return false;
  }
  return readCacheVersion_ == version_ || readCacheComplete_;
}

void TelemetryChannel::InvalidateReadCache() {
  readCache_.clear();
  hasReadCache_ = false;
  readCacheComplete_ = false;
}
} // namespace telemetry
