#define MCAP_IMPLEMENTATION
#include <mcap/mcap.hpp>

#include "contract/telemetry/mcap/McapTelemetryRecorder.hpp"

#include "contract/telemetry/TelemetryTime.hpp"
#include "contract/telemetry/mcap/McapTelemetrySchema.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifndef JSB_TEST_APPLICATION_VERSION
#define JSB_TEST_APPLICATION_VERSION "unknown"
#endif

#ifndef JSB_TEST_GIT_COMMIT
#define JSB_TEST_GIT_COMMIT "unknown"
#endif

#ifndef JSB_TEST_BUILD_TYPE
#define JSB_TEST_BUILD_TYPE "unknown"
#endif

#ifndef JSB_RUNTIME_BRANCH
#define JSB_RUNTIME_BRANCH "unknown"
#endif

#ifndef JSB_CONTRACT_VERSION
#define JSB_CONTRACT_VERSION "unknown"
#endif

#ifndef JSB_CONTRACT_MAJOR_VERSION
#define JSB_CONTRACT_MAJOR_VERSION 0
#endif

#ifndef JSB_TELEMETRY_SCHEMA_VERSION
#define JSB_TELEMETRY_SCHEMA_VERSION 0
#endif

namespace telemetry::recording {
namespace {
constexpr std::string_view PrimaryRollHoldTopic = "/jsb/primary/control/roll";
constexpr std::string_view PrimaryAircraftStateTopic =
    "/jsb/primary/aircraft/state";
constexpr std::string_view BaselineRollHoldTopic = "/jsb/baseline/control/roll";
constexpr std::string_view BaselineAircraftStateTopic =
    "/jsb/baseline/aircraft/state";
constexpr std::string_view ScenarioEventTopic = "/jsb/simulation/event";
constexpr std::string_view PrimarySettingsTopic = "/primary/roll_hold/settings";
constexpr std::string_view BaselineSettingsTopic =
    "/baseline/roll_hold/settings";

class SafeFileWritable final : public mcap::IWritable {
public:
  bool Open(const std::filesystem::path &path) {
    stream_.open(path, std::ios::binary | std::ios::trunc);
    if (!stream_.is_open()) {
      error_ = "failed to open output file";
      return false;
    }
    return true;
  }

  void handleWrite(const std::byte *data, std::uint64_t size) override {
    if (failed_ || !stream_.is_open()) {
      return;
    }
    stream_.write(reinterpret_cast<const char *>(data),
        static_cast<std::streamsize>(size));
    if (!stream_) {
      failed_ = true;
      error_ = "output stream write failed";
      return;
    }
    size_ += size;
  }

  void flush() override {
    if (!failed_ && stream_.is_open()) {
      stream_.flush();
      if (!stream_) {
        failed_ = true;
        error_ = "output stream flush failed";
      }
    }
  }

  void end() override {
    if (!stream_.is_open()) {
      return;
    }
    flush();
    stream_.close();
    if (stream_.fail() && error_.empty()) {
      failed_ = true;
      error_ = "output stream close failed";
    }
  }

  std::uint64_t size() const override { return size_; }
  bool Failed() const { return failed_; }
  const std::string &GetError() const { return error_; }

private:
  std::ofstream stream_;
  std::uint64_t size_ = 0;
  bool failed_ = false;
  std::string error_;
};

std::string FormatDouble(double value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(17) << value;
  return stream.str();
}

std::string GetWallClockTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

std::string GetPlatformName() {
#ifdef _WIN32
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

std::string GetCompilerName() {
#if defined(_MSC_VER)
  return "msvc-" + std::to_string(_MSC_VER);
#elif defined(__clang__)
  return "clang-" + std::string(__clang_version__);
#elif defined(__GNUC__)
  return "gcc-" + std::string(__VERSION__);
#else
  return "unknown";
#endif
}
} // namespace

class McapTelemetryRecorder::Implementation {
public:
  struct RegisteredChannels {
    mcap::ChannelId primaryRollHold = 0;
    mcap::ChannelId primaryAircraftState = 0;
    mcap::ChannelId baselineRollHold = 0;
    mcap::ChannelId baselineAircraftState = 0;
    mcap::ChannelId scenarioEvent = 0;
    mcap::ChannelId primarySettings = 0;
    mcap::ChannelId baselineSettings = 0;
  };

  struct RegisteredSchemas {
    mcap::SchemaId rollControl = 0;
    mcap::SchemaId aircraftState = 0;
    mcap::SchemaId scenarioEvent = 0;
    mcap::SchemaId primarySettings = 0;
    mcap::SchemaId baselineSettings = 0;
  };

  bool Start(const TelemetryRecordingConfig &newConfig,
      const RecordingMetadata &metadata) {
    if (status_.state == RecordingState::Recording) {
      return false;
    }
    ResetForStart(newConfig);
    if (config_.outputPath.empty()) {
      return FailStart("recording output path is empty");
    }
    if (config_.compression != RecordingCompression::None) {
      return FailStart("Zstd compression is not enabled in this build");
    }

    std::error_code error;
    const std::filesystem::path parent = config_.outputPath.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, error);
      if (error) {
        return FailStart(
            "failed to create recording directory: " + error.message());
      }
    }

    sink_ = std::make_unique<SafeFileWritable>();
    if (!sink_->Open(config_.outputPath)) {
      return FailStart(sink_->GetError());
    }

    try {
      writer_ = std::make_unique<mcap::McapWriter>();
      mcap::McapWriterOptions options("");
      options.compression = mcap::Compression::None;
      options.noChunking = false;
      options.noMessageIndex = false;
      options.noSummary = false;
      writer_->open(*sink_, options);
      if (sink_->Failed()) {
        return FailStart(sink_->GetError());
      }
      if (!WriteMetadata(metadata)) {
        return false;
      }
    } catch (const std::exception &exception) {
      return FailStart(exception.what());
    } catch (...) {
      return FailStart("unknown exception while starting recording");
    }

    status_.state = RecordingState::Recording;
    std::cerr << "[MCAP] Recording started: " << config_.outputPath << '\n';
    return true;
  }

  void Stop() noexcept {
    if (status_.state != RecordingState::Recording) {
      return;
    }
    try {
      writer_->close();
      status_.stats.bytesWritten = sink_ ? sink_->size() : 0;
      if (sink_ && sink_->Failed()) {
        SetError(sink_->GetError());
        return;
      }
      status_.state = RecordingState::Idle;
      std::cerr << "[MCAP] Recording stopped: " << status_.stats.messagesWritten
                << " messages\n";
    } catch (const std::exception &exception) {
      SetError(exception.what());
    } catch (...) {
      SetError("unknown exception while finalizing recording");
    }
    writer_.reset();
    sink_.reset();
  }

  RecordingStatus GetStatus() const {
    RecordingStatus result = status_;
    if (sink_) {
      result.stats.bytesWritten = sink_->size();
    }
    return result;
  }

  void Record(const TelemetryFrame &frame) noexcept {
    if (status_.state != RecordingState::Recording) {
      return;
    }
    const auto timestamp = SimulationTimeToNanoseconds(frame.simulationTimeSec);
    if (!timestamp) {
      ++status_.stats.serializationErrors;
      return;
    }
    if (!firstTimestampSec_) {
      firstTimestampSec_ = frame.simulationTimeSec;
    }
    status_.elapsedSimulationSec =
        std::max(0.0, frame.simulationTimeSec - *firstTimestampSec_);

    try {
      if (config_.recordPrimary && frame.primary) {
        RecordSource(*frame.primary, *timestamp, true);
      }
      if (config_.recordBaseline && frame.baseline) {
        RecordSource(*frame.baseline, *timestamp, false);
      }
    } catch (const std::exception &exception) {
      SetError(exception.what());
    } catch (...) {
      SetError("unknown exception while recording telemetry");
    }
  }

  void RecordScenarioEvent(const ScenarioEvent &event) noexcept {
    if (status_.state != RecordingState::Recording) {
      return;
    }
    const auto timestamp = SimulationTimeToNanoseconds(event.simulationTimeSec);
    const auto payload = mcap_schema::Serialize(event, *timestamp);
    if (!timestamp || !payload) {
      ++status_.stats.serializationErrors;
      return;
    }
    try {
      EnsureChannel(channels_.scenarioEvent,
          ScenarioEventTopic,
          schemas_.scenarioEvent,
          mcap_schema::SimulationEventSchemaName,
          mcap_schema::GetSimulationEventProtobufSchema(),
          "protobuf",
          "protobuf");
      WritePayload(channels_.scenarioEvent, *timestamp, *payload);
    } catch (const std::exception &exception) {
      SetError(exception.what());
    } catch (...) {
      SetError("unknown exception while recording a scenario event");
    }
  }

  void RecordPrimarySettings(const PrimaryRollHoldSettings &settings) noexcept {
    if (status_.state != RecordingState::Recording) {
      return;
    }
    try {
      RecordSettings(settings.simulationTimeSec,
          channels_.primarySettings,
          PrimarySettingsTopic,
          schemas_.primarySettings,
          mcap_schema::PrimarySettingsSchemaName,
          mcap_schema::GetPrimarySettingsJsonSchema(),
          settings);
    } catch (const std::exception &exception) {
      SetError(exception.what());
    } catch (...) {
      SetError("unknown exception while recording primary settings");
    }
  }

  void RecordBaselineSettings(
      const BaselineRollHoldSettings &settings) noexcept {
    if (status_.state != RecordingState::Recording) {
      return;
    }
    try {
      RecordSettings(settings.simulationTimeSec,
          channels_.baselineSettings,
          BaselineSettingsTopic,
          schemas_.baselineSettings,
          mcap_schema::BaselineSettingsSchemaName,
          mcap_schema::GetBaselineSettingsJsonSchema(),
          settings);
    } catch (const std::exception &exception) {
      SetError(exception.what());
    } catch (...) {
      SetError("unknown exception while recording baseline settings");
    }
  }

private:
  void ResetForStart(const TelemetryRecordingConfig &newConfig) {
    if (writer_) {
      writer_->terminate();
    }
    if (sink_) {
      sink_->end();
    }
    writer_.reset();
    sink_.reset();
    config_ = newConfig;
    channels_ = {};
    schemas_ = {};
    sequences_.clear();
    firstTimestampSec_.reset();
    status_ = RecordingStatus{};
    status_.outputPath = config_.outputPath;
  }

  bool FailStart(std::string message) {
    if (writer_) {
      writer_->terminate();
    }
    if (sink_) {
      sink_->end();
      status_.stats.bytesWritten = sink_->size();
    }
    writer_.reset();
    sink_.reset();
    status_.state = RecordingState::Error;
    status_.errorMessage = std::move(message);
    std::cerr << "[MCAP] Recording error: " << status_.errorMessage << '\n';
    return false;
  }

  void SetError(std::string message) noexcept {
    if (writer_) {
      writer_->terminate();
    }
    if (sink_) {
      sink_->end();
      status_.stats.bytesWritten = sink_->size();
    }
    writer_.reset();
    sink_.reset();
    status_.state = RecordingState::Error;
    status_.errorMessage = std::move(message);
    std::cerr << "[MCAP] Recording error: " << status_.errorMessage << '\n';
  }

  mcap::SchemaId RegisterSchema(std::string_view name,
      std::string_view schemaText, std::string_view schemaEncoding) {
    mcap::Schema schema(name, schemaEncoding, schemaText);
    writer_->addSchema(schema);
    return schema.id;
  }

  mcap::ChannelId RegisterChannel(std::string_view topic,
      mcap::SchemaId schemaId, std::string_view messageEncoding) {
    mcap::Channel channel(topic,
        messageEncoding,
        schemaId,
        {{"time_basis", "simulation_time"}});
    writer_->addChannel(channel);
    sequences_.emplace(channel.id, 0);
    return channel.id;
  }

  void EnsureChannel(mcap::ChannelId &channelId, std::string_view topic,
      mcap::SchemaId &schemaId, std::string_view schemaName,
      std::string_view schemaText,
      std::string_view schemaEncoding = "jsonschema",
      std::string_view messageEncoding = "json") {
    if (channelId != 0) {
      return;
    }
    if (schemaId == 0) {
      schemaId = RegisterSchema(schemaName, schemaText, schemaEncoding);
    }
    channelId = RegisterChannel(topic, schemaId, messageEncoding);
  }

  bool WriteMetadata(const RecordingMetadata &metadata) {
    mcap::Metadata record;
    record.name = "jsb0.run";
    record.metadata = {
        {"format_version", "1"},
        {"contract_version",
            metadata.contractVersion.empty() ? JSB_CONTRACT_VERSION
                                             : metadata.contractVersion},
        {"contract_major_version", std::to_string(JSB_CONTRACT_MAJOR_VERSION)},
        {"telemetry_schema_version",
            std::to_string(metadata.telemetrySchemaVersion == 0
                               ? JSB_TELEMETRY_SCHEMA_VERSION
                               : metadata.telemetrySchemaVersion)},
        {"application_version",
            metadata.applicationVersion.empty() ? JSB_TEST_APPLICATION_VERSION
                                                : metadata.applicationVersion},
        {"git_commit",
            metadata.gitCommit.empty() ? JSB_TEST_GIT_COMMIT
                                       : metadata.gitCommit},
        {"runtime_branch",
            metadata.runtimeBranch.empty() ? JSB_RUNTIME_BRANCH
                                           : metadata.runtimeBranch},
        {"aircraft", metadata.aircraft},
        {"scenario_name", metadata.scenarioName},
        {"scenario_file", metadata.scenarioFile},
        {"scenario_digest", metadata.scenarioDigest},
        {"scenario_schema_version",
            std::to_string(metadata.scenarioSchemaVersion)},
        {"scenario_type", metadata.scenarioType},
        {"scenario_duration_sec", FormatDouble(metadata.scenarioDurationSec)},
        {"simulation_dt_sec", FormatDouble(metadata.simulationDtSec)},
        {"execution_mode", metadata.executionMode},
        {"execution_variant", metadata.executionVariant},
        {"execution_variants", metadata.executionVariants},
        {"primary_autopilot", metadata.primaryAutopilot},
        {"resolved_autopilot",
            metadata.executionMode == "compare" ? std::string{}
                                                : metadata.primaryAutopilot},
        {"baseline_autopilot", metadata.baselineAutopilot},
        {"created_at_wall_clock", GetWallClockTimestamp()},
        {"build_type", JSB_TEST_BUILD_TYPE},
        {"platform", GetPlatformName()},
        {"compiler", GetCompilerName()},
    };
    const mcap::Status result = writer_->write(record);
    if (!result.ok()) {
      SetError(result.message);
      return false;
    }
    if (sink_->Failed()) {
      SetError(sink_->GetError());
      return false;
    }
    return true;
  }

  template <typename Value>
  void SerializeAndWrite(mcap::ChannelId &channelId, std::string_view topic,
      mcap::SchemaId &schemaId, std::string_view schemaName,
      std::string_view schemaText, std::uint64_t timestamp,
      const Value &value) {
    const auto payload = mcap_schema::Serialize(value);
    if (!payload) {
      ++status_.stats.serializationErrors;
      return;
    }
    EnsureChannel(channelId, topic, schemaId, schemaName, schemaText);
    WritePayload(channelId, timestamp, *payload);
  }

  template <typename Settings>
  void RecordSettings(double simulationTimeSec, mcap::ChannelId &channelId,
      std::string_view topic, mcap::SchemaId &schemaId,
      std::string_view schemaName, std::string_view schemaText,
      const Settings &settings) {
    if (status_.state != RecordingState::Recording) {
      return;
    }
    const auto timestamp = SimulationTimeToNanoseconds(simulationTimeSec);
    if (!timestamp) {
      ++status_.stats.serializationErrors;
      return;
    }
    SerializeAndWrite(channelId,
        topic,
        schemaId,
        schemaName,
        schemaText,
        *timestamp,
        settings);
  }

  void RecordSource(const TelemetrySourceFrame &source, std::uint64_t timestamp,
      bool primary) {
    mcap::ChannelId &rollHoldChannel =
        primary ? channels_.primaryRollHold : channels_.baselineRollHold;
    mcap::ChannelId &aircraftStateChannel =
        primary ? channels_.primaryAircraftState
                : channels_.baselineAircraftState;
    const std::string_view rollHoldTopic =
        primary ? PrimaryRollHoldTopic : BaselineRollHoldTopic;
    const std::string_view aircraftStateTopic =
        primary ? PrimaryAircraftStateTopic : BaselineAircraftStateTopic;
    if (config_.recordControllerDiagnostics && source.rollHold) {
      const auto payload = mcap_schema::Serialize(*source.rollHold, timestamp);
      if (!payload) {
        ++status_.stats.serializationErrors;
      } else {
        EnsureChannel(rollHoldChannel,
            rollHoldTopic,
            schemas_.rollControl,
            mcap_schema::RollControlStateSchemaName,
            mcap_schema::GetRollControlStateProtobufSchema(),
            "protobuf",
            "protobuf");
        WritePayload(rollHoldChannel, timestamp, *payload);
      }
    }
    if (config_.recordAircraftState && source.aircraftState) {
      const auto payload =
          mcap_schema::Serialize(*source.aircraftState, timestamp);
      if (!payload) {
        ++status_.stats.serializationErrors;
      } else {
        EnsureChannel(aircraftStateChannel,
            aircraftStateTopic,
            schemas_.aircraftState,
            mcap_schema::AircraftStateSchemaName,
            mcap_schema::GetAircraftStateProtobufSchema(),
            "protobuf",
            "protobuf");
        WritePayload(aircraftStateChannel, timestamp, *payload);
      }
    }
  }

  void WritePayload(mcap::ChannelId channelId, std::uint64_t timestamp,
      const std::string &payload) {
    if (status_.state != RecordingState::Recording || channelId == 0) {
      return;
    }
    mcap::Message message;
    message.channelId = channelId;
    message.sequence = sequences_[channelId]++;
    message.logTime = timestamp;
    message.publishTime = timestamp;
    message.data = reinterpret_cast<const std::byte *>(payload.data());
    message.dataSize = payload.size();
    const mcap::Status result = writer_->write(message);
    if (!result.ok()) {
      SetError(result.message);
      return;
    }
    if (sink_->Failed()) {
      SetError(sink_->GetError());
      return;
    }
    ++status_.stats.messagesWritten;
    status_.stats.bytesWritten = sink_->size();
  }

  TelemetryRecordingConfig config_;
  RecordingStatus status_;
  RegisteredChannels channels_;
  RegisteredSchemas schemas_;
  std::map<mcap::ChannelId, std::uint32_t> sequences_;
  std::optional<double> firstTimestampSec_;
  std::unique_ptr<SafeFileWritable> sink_;
  std::unique_ptr<mcap::McapWriter> writer_;
};

McapTelemetryRecorder::McapTelemetryRecorder()
    : implementation_(std::make_unique<Implementation>()) {}

McapTelemetryRecorder::~McapTelemetryRecorder() { Stop(); }

bool McapTelemetryRecorder::Start(const TelemetryRecordingConfig &config,
    const RecordingMetadata &metadata) {
  return implementation_->Start(config, metadata);
}

void McapTelemetryRecorder::Stop() noexcept { implementation_->Stop(); }

RecordingStatus McapTelemetryRecorder::GetStatus() const {
  return implementation_->GetStatus();
}

void McapTelemetryRecorder::Record(const TelemetryFrame &frame) noexcept {
  implementation_->Record(frame);
}

void McapTelemetryRecorder::RecordScenarioEvent(
    const ScenarioEvent &event) noexcept {
  implementation_->RecordScenarioEvent(event);
}

void McapTelemetryRecorder::RecordPrimarySettings(
    const PrimaryRollHoldSettings &settings) noexcept {
  implementation_->RecordPrimarySettings(settings);
}

void McapTelemetryRecorder::RecordBaselineSettings(
    const BaselineRollHoldSettings &settings) noexcept {
  implementation_->RecordBaselineSettings(settings);
}
} // namespace telemetry::recording
