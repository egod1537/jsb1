#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace telemetry::recording {
enum class RecordingState {
  Idle,
  Recording,
  Error,
};

enum class RecordingCompression {
  None,
  Zstd,
};

struct TelemetryRecordingConfig {
  std::filesystem::path outputPath;
  bool recordPrimary = true;
  bool recordBaseline = true;
  bool recordControllerDiagnostics = true;
  bool recordAircraftState = true;
  RecordingCompression compression = RecordingCompression::None;
};

struct RecordingMetadata {
  std::string contractVersion;
  std::uint32_t telemetrySchemaVersion = 0;
  std::string applicationVersion;
  std::string gitCommit;
  std::string runtimeBranch;
  std::string aircraft;
  std::string scenarioName;
  std::string scenarioFile;
  std::string scenarioDigest;
  std::uint32_t scenarioSchemaVersion = 0;
  std::string scenarioType;
  double scenarioDurationSec = 0.0;
  double simulationDtSec = 0.0;
  std::string executionMode;
  std::string executionVariant;
  std::string executionVariants;
  std::string primaryAutopilot;
  std::string baselineAutopilot;
};

struct RollHoldDiagnostics {
  double commandedRollRad = 0.0;
  double rollRad = 0.0;
  double rollErrorRad = 0.0;
  double commandedRollRateRadPerSec = 0.0;
  double rollRateRadPerSec = 0.0;
  double rollRateErrorRadPerSec = 0.0;
  double aileronCommand = 0.0;

  bool operator==(const RollHoldDiagnostics &) const = default;
};

struct AircraftRollState {
  double rollRad = 0.0;
  double rollRateRadPerSec = 0.0;
};

struct ControlInputState {
  double aileronCommand = 0.0;
};

struct TelemetrySourceFrame {
  std::optional<RollHoldDiagnostics> rollHold;
  std::optional<AircraftRollState> aircraftState;
  std::optional<ControlInputState> controlInput;
};

struct TelemetryFrame {
  double simulationTimeSec = 0.0;
  std::optional<TelemetrySourceFrame> primary;
  std::optional<TelemetrySourceFrame> baseline;
};

struct ScenarioEvent {
  double simulationTimeSec = 0.0;
  std::string type;
  std::optional<double> targetRollRad;
};

struct PrimaryRollHoldSettings {
  double simulationTimeSec = 0.0;
  double rollAngleProportionalGain = 0.0;
  double rollRateProportionalGain = 0.0;
};

struct BaselineRollHoldSettings {
  double simulationTimeSec = 0.0;
  double rollTimeConstantSec = 0.0;
  double maximumRollRateRadPerSec = 0.0;
  double rateProportionalGain = 0.0;
  double rateIntegralGain = 0.0;
  double rateDerivativeGain = 0.0;
  double rateFeedForwardGain = 0.0;
  double integratorLimit = 0.0;
};

struct RecordingStats {
  std::uint64_t messagesWritten = 0;
  std::uint64_t bytesWritten = 0;
  std::uint64_t serializationErrors = 0;
};

struct RecordingStatus {
  RecordingState state = RecordingState::Idle;
  std::filesystem::path outputPath;
  std::string errorMessage;
  double elapsedSimulationSec = 0.0;
  RecordingStats stats;
};
} // namespace telemetry::recording
