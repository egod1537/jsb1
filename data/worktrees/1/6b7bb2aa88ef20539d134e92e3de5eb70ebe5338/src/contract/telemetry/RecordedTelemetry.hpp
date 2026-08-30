#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <string>

namespace telemetry::recording {
struct RecordedTimeRange {
  double startTimeSec = 0.0;
  double endTimeSec = 0.0;
};

struct RecordedRunInfo {
  std::string contractVersion;
  std::uint32_t telemetrySchemaVersion = 0;
  std::string scenarioName;
  std::string scenarioFile;
  std::string scenarioDigest;
  std::uint32_t scenarioSchemaVersion = 0;
  std::string scenarioType;
  std::string gitCommit;
  std::string runtimeBranch;
  std::string applicationVersion;
  std::string aircraft;
  std::string executionMode;
  std::string executionVariant;
  std::string executionVariants;
  std::string primaryAutopilot;
  std::string resolvedAutopilot;
  std::string baselineAutopilot;
  std::string createdAtWallClock;
  double scenarioDurationSec = 0.0;
  double simulationDtSec = 0.0;
  double startTimeSec = 0.0;
  double endTimeSec = 0.0;
  std::map<std::string, std::string, std::less<>> metadata;
};

struct RecordedChannelInfo {
  std::string topic;
  std::string schemaName;
  std::string messageEncoding;
  std::string schemaEncoding;
  std::size_t schemaDataSize = 0;
  std::string schemaData;
};

struct RecordedSample {
  std::string topic;
  std::string schemaName;
  std::uint64_t logTimeNanoseconds = 0;
  std::uint64_t publishTimeNanoseconds = 0;
  double simulationTimeSec = 0.0;
  std::string payload;
};
} // namespace telemetry::recording
