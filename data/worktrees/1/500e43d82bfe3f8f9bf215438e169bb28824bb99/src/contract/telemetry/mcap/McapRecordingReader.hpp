#pragma once

#include "contract/telemetry/RecordedTelemetry.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace telemetry::recording {
class McapRecordingReader {
public:
  McapRecordingReader();
  ~McapRecordingReader();

  McapRecordingReader(const McapRecordingReader &) = delete;
  McapRecordingReader &operator=(const McapRecordingReader &) = delete;
  McapRecordingReader(McapRecordingReader &&) noexcept;
  McapRecordingReader &operator=(McapRecordingReader &&) noexcept;

  // File lifecycle and run summary
  bool Open(const std::filesystem::path &path);
  void Close();
  bool IsOpen() const;
  const std::string &GetLastError() const;
  const RecordedRunInfo &GetRunInfo() const;
  const std::vector<RecordedChannelInfo> &GetChannels() const;

  // Timestamp-ordered access
  std::vector<RecordedSample> ReadMessages(std::string_view topic = {},
      std::optional<RecordedTimeRange> timeRange = std::nullopt);

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};
} // namespace telemetry::recording
