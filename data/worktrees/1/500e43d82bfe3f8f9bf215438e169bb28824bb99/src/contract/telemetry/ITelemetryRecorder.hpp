#pragma once

#include "contract/telemetry/RecordingTypes.hpp"

namespace telemetry::recording {
class ITelemetryRecorder {
public:
  virtual ~ITelemetryRecorder() = default;

  virtual bool Start(const TelemetryRecordingConfig &config,
      const RecordingMetadata &metadata) = 0;
  virtual void Stop() noexcept = 0;

  virtual RecordingStatus GetStatus() const = 0;
  virtual void Record(const TelemetryFrame &frame) noexcept = 0;
  virtual void RecordScenarioEvent(const ScenarioEvent &event) noexcept = 0;
  virtual void RecordPrimarySettings(
      const PrimaryRollHoldSettings &settings) noexcept = 0;
  virtual void RecordBaselineSettings(
      const BaselineRollHoldSettings &settings) noexcept = 0;
};
} // namespace telemetry::recording
