#include "sim/telemetry/recording/TelemetryRecordingService.hpp"

#include "sim/telemetry/AircraftTelemetry.hpp"
#include "sim/telemetry/AutopilotTelemetry.hpp"
#include "sim/telemetry/TelemetryChannel.hpp"
#include "sim/telemetry/TelemetryRegistry.hpp"
#include "contract/telemetry/mcap/McapTelemetryRecorder.hpp"
#include "common/math/Math.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace telemetry::recording {
namespace {
std::optional<double> LatestValue(const TelemetryRegistry &registry,
    std::string_view path, double simulationTimeSec) {
  const TelemetryChannel *channel = registry.Find(path);
  const TelemetrySample *sample = channel ? channel->GetLatest() : nullptr;
  constexpr double TimestampToleranceSec = 1.0e-9;
  if (sample == nullptr || !std::isfinite(sample->value)
      || !std::isfinite(sample->timeSec)
      || std::abs(sample->timeSec - simulationTimeSec)
             > TimestampToleranceSec) {
    return std::nullopt;
  }
  return sample->value;
}

std::optional<TelemetrySourceFrame> CaptureTelemetrySource(
    const TelemetryRegistry &registry, double simulationTimeSec) {
  TelemetrySourceFrame source;

  const auto commandedRoll = LatestValue(registry,
      paths::AutopilotRollHoldCommandedRoll,
      simulationTimeSec);
  const auto roll =
      LatestValue(registry, paths::AutopilotRollHoldRoll, simulationTimeSec);
  const auto rollError = LatestValue(registry,
      paths::AutopilotRollHoldRollError,
      simulationTimeSec);
  const auto commandedRollRate = LatestValue(registry,
      paths::AutopilotRollHoldCommandedRollRate,
      simulationTimeSec);
  const auto rollRate = LatestValue(registry,
      paths::AutopilotRollHoldRollRate,
      simulationTimeSec);
  const auto rollRateError = LatestValue(registry,
      paths::AutopilotRollHoldRollRateError,
      simulationTimeSec);
  const auto rollHoldAileron = LatestValue(registry,
      paths::AutopilotRollHoldAileronCommand,
      simulationTimeSec);
  if (commandedRoll && roll && rollError && commandedRollRate && rollRate
      && rollRateError && rollHoldAileron) {
    source.rollHold = RollHoldDiagnostics{
        .commandedRollRad = math::DegToRad(*commandedRoll),
        .rollRad = math::DegToRad(*roll),
        .rollErrorRad = math::DegToRad(*rollError),
        .commandedRollRateRadPerSec = math::DegToRad(*commandedRollRate),
        .rollRateRadPerSec = math::DegToRad(*rollRate),
        .rollRateErrorRadPerSec = math::DegToRad(*rollRateError),
        .aileronCommand = *rollHoldAileron,
    };
  }

  const auto aircraftRoll =
      LatestValue(registry, paths::AircraftAttitudeRoll, simulationTimeSec);
  const auto aircraftRollRate =
      LatestValue(registry, paths::AircraftRateP, simulationTimeSec);
  const auto aileron =
      LatestValue(registry, paths::AircraftControlAileron, simulationTimeSec);
  if (!source.rollHold && commandedRoll && aircraftRoll && aircraftRollRate
      && aileron) {
    const double commandedRollRad = math::DegToRad(*commandedRoll);
    const double rollRad = math::DegToRad(*aircraftRoll);
    const double rollRateRadPerSec = math::DegToRad(*aircraftRollRate);
    source.rollHold = RollHoldDiagnostics{
        .commandedRollRad = commandedRollRad,
        .rollRad = rollRad,
        .rollErrorRad = commandedRollRad - rollRad,
        .commandedRollRateRadPerSec = 0.0,
        .rollRateRadPerSec = rollRateRadPerSec,
        .rollRateErrorRadPerSec = -rollRateRadPerSec,
        .aileronCommand = *aileron,
    };
  }
  if (aircraftRoll && aircraftRollRate) {
    source.aircraftState = AircraftRollState{math::DegToRad(*aircraftRoll),
        math::DegToRad(*aircraftRollRate)};
  }

  if (aileron) {
    source.controlInput = ControlInputState{*aileron};
  }

  return source.rollHold || source.aircraftState || source.controlInput
             ? std::optional<TelemetrySourceFrame>(source)
             : std::nullopt;
}

std::tm GetLocalTime(std::time_t time) {
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  return local;
}
} // namespace

TelemetryRecordingService::TelemetryRecordingService()
    : TelemetryRecordingService(std::make_unique<McapTelemetryRecorder>()) {}

TelemetryRecordingService::TelemetryRecordingService(
    std::unique_ptr<ITelemetryRecorder> recorder)
    : recorder_(std::move(recorder)) {}

TelemetryRecordingService::~TelemetryRecordingService() { Stop(); }

bool TelemetryRecordingService::Start(const TelemetryRecordingConfig &config,
    const RecordingMetadata &metadata) {
  return recorder_ && recorder_->Start(config, metadata);
}

bool TelemetryRecordingService::StartDefault(const RecordingMetadata &metadata,
    std::string_view recordingName) {
  const std::string_view name = recordingName.empty()
                                    ? std::string_view(metadata.scenarioName)
                                    : recordingName;
  TelemetryRecordingConfig config;
  config.outputPath =
      MakeAutomaticRecordingPath(GetDefaultRecordingsDirectory(),
          name.empty() ? std::string_view(metadata.aircraft) : name);
  return Start(config, metadata);
}

void TelemetryRecordingService::Stop() noexcept {
  if (recorder_) {
    recorder_->Stop();
  }
}

RecordingStatus TelemetryRecordingService::GetStatus() const {
  return recorder_ ? recorder_->GetStatus() : RecordingStatus{};
}

void TelemetryRecordingService::Consume(double simulationTimeSec,
    const TelemetryRegistry &primary,
    const TelemetryRegistry *baseline) noexcept {
  if (!recorder_ || recorder_->GetStatus().state != RecordingState::Recording) {
    return;
  }
  TelemetryFrame frame;
  frame.simulationTimeSec = simulationTimeSec;
  frame.primary = CaptureTelemetrySource(primary, simulationTimeSec);
  if (baseline != nullptr) {
    frame.baseline = CaptureTelemetrySource(*baseline, simulationTimeSec);
  }
  Consume(frame);
}

void TelemetryRecordingService::Consume(const TelemetryFrame &frame) noexcept {
  if (recorder_ && recorder_->GetStatus().state == RecordingState::Recording) {
    recorder_->Record(frame);
  }
}

void TelemetryRecordingService::RecordScenarioEvent(
    const ScenarioEvent &event) noexcept {
  if (recorder_) {
    recorder_->RecordScenarioEvent(event);
  }
}

void TelemetryRecordingService::RecordPrimarySettings(
    const PrimaryRollHoldSettings &settings) noexcept {
  if (recorder_) {
    recorder_->RecordPrimarySettings(settings);
  }
}

void TelemetryRecordingService::RecordBaselineSettings(
    const BaselineRollHoldSettings &settings) noexcept {
  if (recorder_) {
    recorder_->RecordBaselineSettings(settings);
  }
}

std::optional<TelemetrySourceFrame> TelemetryRecordingService::CaptureSource(
    const TelemetryRegistry &registry, double simulationTimeSec) {
  return CaptureTelemetrySource(registry, simulationTimeSec);
}

std::filesystem::path
TelemetryRecordingService::GetDefaultRecordingsDirectory() {
  std::error_code error;
  const std::filesystem::path current = std::filesystem::current_path(error);
  return (error ? std::filesystem::path{} : current) / "recordings";
}

std::string TelemetryRecordingService::SanitizeRecordingName(
    std::string_view name) {
  std::string sanitized;
  sanitized.reserve(name.size());
  bool previousWasSeparator = false;
  for (const unsigned char character : name) {
    const bool valid =
        std::isalnum(character) != 0 || character == '-' || character == '_';
    if (valid) {
      sanitized.push_back(static_cast<char>(character));
      previousWasSeparator = false;
    } else if (!sanitized.empty() && !previousWasSeparator) {
      sanitized.push_back('_');
      previousWasSeparator = true;
    }
  }
  while (!sanitized.empty()
         && (sanitized.back() == '_' || sanitized.back() == '.')) {
    sanitized.pop_back();
  }
  return sanitized.empty() ? "telemetry" : sanitized;
}

std::filesystem::path TelemetryRecordingService::MakeAutomaticRecordingPath(
    const std::filesystem::path &directory, std::string_view name) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  const std::tm local = GetLocalTime(time);
  std::ostringstream filename;
  filename << std::put_time(&local, "%Y%m%d_%H%M%S") << '_'
           << SanitizeRecordingName(name);
  const std::string stem = filename.str();
  std::filesystem::path candidate = directory / (stem + ".mcap");
  std::error_code error;
  for (unsigned int suffix = 2;
      std::filesystem::exists(candidate, error) && !error;
      ++suffix) {
    candidate = directory / (stem + '_' + std::to_string(suffix) + ".mcap");
  }
  return candidate;
}

} // namespace telemetry::recording
