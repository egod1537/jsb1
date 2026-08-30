#include "contract/telemetry/mcap/McapRecordingReader.hpp"

#include "contract/telemetry/TelemetryTime.hpp"

#include <mcap/reader.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>
#include <utility>

namespace telemetry::recording {
namespace {
double ParseDouble(
    const std::map<std::string, std::string, std::less<>> &values,
    std::string_view key) {
  const auto value = values.find(key);
  if (value == values.end()) {
    return 0.0;
  }
  double parsed = 0.0;
  const auto result = std::from_chars(value->second.data(),
      value->second.data() + value->second.size(),
      parsed);
  return result.ec == std::errc{} ? parsed : 0.0;
}

std::uint32_t ParseUint(
    const std::map<std::string, std::string, std::less<>> &values,
    std::string_view key) {
  const auto value = values.find(key);
  if (value == values.end()) {
    return 0;
  }
  std::uint32_t parsed = 0;
  const auto result = std::from_chars(value->second.data(),
      value->second.data() + value->second.size(),
      parsed);
  return result.ec == std::errc{} ? parsed : 0;
}

std::string FindValue(
    const std::map<std::string, std::string, std::less<>> &values,
    std::string_view key) {
  const auto value = values.find(key);
  return value == values.end() ? std::string{} : value->second;
}
} // namespace

class McapRecordingReader::Implementation {
public:
  bool Open(const std::filesystem::path &path) {
    Close();
    const mcap::Status openStatus = reader_.open(path.string());
    if (!openStatus.ok()) {
      lastError_ = openStatus.message;
      return false;
    }
    open_ = true;

    const mcap::Status summaryStatus =
        reader_.readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
    if (!summaryStatus.ok()) {
      lastError_ = summaryStatus.message;
      CloseReaderOnly();
      return false;
    }
    if (!ReadMetadata() || !ReadChannels()) {
      CloseReaderOnly();
      return false;
    }

    if (const auto &statistics = reader_.statistics(); statistics) {
      if (statistics->messageCount > 0) {
        runInfo_.startTimeSec =
            NanosecondsToSimulationTime(statistics->messageStartTime);
        runInfo_.endTimeSec =
            NanosecondsToSimulationTime(statistics->messageEndTime);
      }
    }
    PopulateRunInfo();
    return true;
  }

  void Close() {
    if (open_) {
      reader_.close();
    }
    open_ = false;
    lastError_.clear();
    runInfo_ = {};
    channels_.clear();
  }

  std::vector<RecordedSample> ReadMessages(std::string_view topic,
      std::optional<RecordedTimeRange> timeRange) {
    std::vector<RecordedSample> samples;
    if (!open_) {
      lastError_ = "recording is not open";
      return samples;
    }

    mcap::ReadMessageOptions options;
    options.readOrder = mcap::ReadMessageOptions::ReadOrder::LogTimeOrder;
    const std::string requestedTopic(topic);
    if (!requestedTopic.empty()) {
      options.topicFilter = [requestedTopic](std::string_view candidate) {
        return candidate == requestedTopic;
      };
    }
    if (timeRange) {
      const auto start = SimulationTimeToNanoseconds(timeRange->startTimeSec);
      const auto end = SimulationTimeToNanoseconds(timeRange->endTimeSec);
      if (!start || !end || *end < *start) {
        lastError_ = "invalid simulation time range";
        return samples;
      }
      options.startTime = *start;
      options.endTime =
          *end == std::numeric_limits<std::uint64_t>::max() ? *end : *end + 1;
    }

    mcap::Status iterationStatus;
    const auto onProblem = [&iterationStatus](const mcap::Status &status) {
      iterationStatus = status;
    };
    try {
      for (const auto &view : reader_.readMessages(onProblem, options)) {
        RecordedSample sample;
        sample.topic = view.channel ? view.channel->topic : std::string{};
        sample.schemaName = view.schema ? view.schema->name : std::string{};
        sample.logTimeNanoseconds = view.message.logTime;
        sample.publishTimeNanoseconds = view.message.publishTime;
        sample.simulationTimeSec =
            NanosecondsToSimulationTime(view.message.logTime);
        sample.payload.assign(reinterpret_cast<const char *>(view.message.data),
            static_cast<std::size_t>(view.message.dataSize));
        samples.push_back(std::move(sample));
      }
    } catch (const std::exception &exception) {
      lastError_ = exception.what();
      return {};
    }
    if (!iterationStatus.ok()) {
      lastError_ = iterationStatus.message;
      return {};
    }
    lastError_.clear();
    return samples;
  }

  bool open_ = false;
  std::string lastError_;
  RecordedRunInfo runInfo_;
  std::vector<RecordedChannelInfo> channels_;

private:
  void CloseReaderOnly() {
    reader_.close();
    open_ = false;
    runInfo_ = {};
    channels_.clear();
  }

  bool ReadMetadata() {
    for (const auto &[name, index] : reader_.metadataIndexes()) {
      if (name != "jsb0.run" && name != "jsb_test.run") {
        continue;
      }
      mcap::Record record;
      const mcap::Status readStatus =
          mcap::McapReader::ReadRecord(*reader_.dataSource(),
              index.offset,
              &record);
      if (!readStatus.ok()) {
        lastError_ = readStatus.message;
        return false;
      }
      mcap::Metadata metadata;
      const mcap::Status parseStatus =
          mcap::McapReader::ParseMetadata(record, &metadata);
      if (!parseStatus.ok()) {
        lastError_ = parseStatus.message;
        return false;
      }
      for (const auto &[key, value] : metadata.metadata) {
        runInfo_.metadata.insert_or_assign(key, value);
      }
    }
    return true;
  }

  bool ReadChannels() {
    for (const auto &[id, channel] : reader_.channels()) {
      (void)id;
      if (!channel) {
        continue;
      }
      RecordedChannelInfo info;
      info.topic = channel->topic;
      info.messageEncoding = channel->messageEncoding;
      if (const auto schema = reader_.schema(channel->schemaId); schema) {
        info.schemaName = schema->name;
        info.schemaEncoding = schema->encoding;
        info.schemaDataSize = schema->data.size();
        info.schemaData.assign(
            reinterpret_cast<const char *>(schema->data.data()),
            schema->data.size());
      }
      channels_.push_back(std::move(info));
    }
    std::sort(channels_.begin(),
        channels_.end(),
        [](const RecordedChannelInfo &left, const RecordedChannelInfo &right) {
          return left.topic < right.topic;
        });
    return true;
  }

  void PopulateRunInfo() {
    runInfo_.contractVersion = FindValue(runInfo_.metadata, "contract_version");
    runInfo_.telemetrySchemaVersion =
        ParseUint(runInfo_.metadata, "telemetry_schema_version");
    runInfo_.scenarioName = FindValue(runInfo_.metadata, "scenario_name");
    runInfo_.scenarioFile = FindValue(runInfo_.metadata, "scenario_file");
    runInfo_.scenarioDigest = FindValue(runInfo_.metadata, "scenario_digest");
    runInfo_.scenarioSchemaVersion =
        ParseUint(runInfo_.metadata, "scenario_schema_version");
    runInfo_.scenarioType = FindValue(runInfo_.metadata, "scenario_type");
    runInfo_.gitCommit = FindValue(runInfo_.metadata, "git_commit");
    runInfo_.runtimeBranch = FindValue(runInfo_.metadata, "runtime_branch");
    runInfo_.applicationVersion =
        FindValue(runInfo_.metadata, "application_version");
    runInfo_.aircraft = FindValue(runInfo_.metadata, "aircraft");
    runInfo_.executionMode = FindValue(runInfo_.metadata, "execution_mode");
    runInfo_.executionVariant =
        FindValue(runInfo_.metadata, "execution_variant");
    runInfo_.executionVariants =
        FindValue(runInfo_.metadata, "execution_variants");
    runInfo_.primaryAutopilot =
        FindValue(runInfo_.metadata, "primary_autopilot");
    runInfo_.resolvedAutopilot =
        FindValue(runInfo_.metadata, "resolved_autopilot");
    runInfo_.baselineAutopilot =
        FindValue(runInfo_.metadata, "baseline_autopilot");
    runInfo_.createdAtWallClock =
        FindValue(runInfo_.metadata, "created_at_wall_clock");
    runInfo_.scenarioDurationSec =
        ParseDouble(runInfo_.metadata, "scenario_duration_sec");
    runInfo_.simulationDtSec =
        ParseDouble(runInfo_.metadata, "simulation_dt_sec");
  }

  mcap::McapReader reader_;
};

McapRecordingReader::McapRecordingReader()
    : implementation_(std::make_unique<Implementation>()) {}

McapRecordingReader::~McapRecordingReader() = default;
McapRecordingReader::McapRecordingReader(
    McapRecordingReader &&) noexcept = default;
McapRecordingReader &McapRecordingReader::operator=(
    McapRecordingReader &&) noexcept = default;

bool McapRecordingReader::Open(const std::filesystem::path &path) {
  return implementation_->Open(path);
}

void McapRecordingReader::Close() { implementation_->Close(); }
bool McapRecordingReader::IsOpen() const { return implementation_->open_; }
const std::string &McapRecordingReader::GetLastError() const {
  return implementation_->lastError_;
}
const RecordedRunInfo &McapRecordingReader::GetRunInfo() const {
  return implementation_->runInfo_;
}
const std::vector<RecordedChannelInfo> &
McapRecordingReader::GetChannels() const {
  return implementation_->channels_;
}
std::vector<RecordedSample> McapRecordingReader::ReadMessages(
    std::string_view topic, std::optional<RecordedTimeRange> timeRange) {
  return implementation_->ReadMessages(topic, timeRange);
}
} // namespace telemetry::recording
