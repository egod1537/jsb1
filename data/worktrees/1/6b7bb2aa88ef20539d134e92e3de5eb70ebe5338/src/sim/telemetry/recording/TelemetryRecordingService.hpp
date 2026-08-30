#pragma once

#include "contract/telemetry/ITelemetryRecorder.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace telemetry {
class TelemetryRegistry;
}

namespace telemetry::recording {
class TelemetryRecordingService {
public:
  TelemetryRecordingService();
  explicit TelemetryRecordingService(
      std::unique_ptr<ITelemetryRecorder> recorder);
  ~TelemetryRecordingService();

  TelemetryRecordingService(const TelemetryRecordingService &) = delete;
  TelemetryRecordingService &operator=(
      const TelemetryRecordingService &) = delete;

  // Recording lifecycle
  bool Start(const TelemetryRecordingConfig &config,
      const RecordingMetadata &metadata);
  bool StartDefault(const RecordingMetadata &metadata,
      std::string_view recordingName = {});
  void Stop() noexcept;
  RecordingStatus GetStatus() const;

  // Telemetry and event consumption
  void Consume(double simulationTimeSec, const TelemetryRegistry &primary,
      const TelemetryRegistry *baseline) noexcept;
  void Consume(const TelemetryFrame &frame) noexcept;
  void RecordScenarioEvent(const ScenarioEvent &event) noexcept;
  void RecordPrimarySettings(const PrimaryRollHoldSettings &settings) noexcept;
  void RecordBaselineSettings(
      const BaselineRollHoldSettings &settings) noexcept;
  static std::optional<TelemetrySourceFrame> CaptureSource(
      const TelemetryRegistry &registry, double simulationTimeSec);

  // Output location helpers
  static std::filesystem::path GetDefaultRecordingsDirectory();
  static std::string SanitizeRecordingName(std::string_view name);
  static std::filesystem::path MakeAutomaticRecordingPath(
      const std::filesystem::path &directory, std::string_view name);

private:
  std::unique_ptr<ITelemetryRecorder> recorder_;
};
} // namespace telemetry::recording
