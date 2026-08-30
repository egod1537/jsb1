#pragma once

#include "contract/telemetry/ITelemetryRecorder.hpp"

#include <memory>

namespace telemetry::recording {
class McapTelemetryRecorder final : public ITelemetryRecorder {
public:
  McapTelemetryRecorder();
  ~McapTelemetryRecorder() override;

  McapTelemetryRecorder(const McapTelemetryRecorder &) = delete;
  McapTelemetryRecorder &operator=(const McapTelemetryRecorder &) = delete;

  // Recording lifecycle
  bool Start(const TelemetryRecordingConfig &config,
      const RecordingMetadata &metadata) override;
  void Stop() noexcept override;
  RecordingStatus GetStatus() const override;

  // Telemetry consumption
  void Record(const TelemetryFrame &frame) noexcept override;
  void RecordScenarioEvent(const ScenarioEvent &event) noexcept override;
  void RecordPrimarySettings(
      const PrimaryRollHoldSettings &settings) noexcept override;
  void RecordBaselineSettings(
      const BaselineRollHoldSettings &settings) noexcept override;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};
} // namespace telemetry::recording
