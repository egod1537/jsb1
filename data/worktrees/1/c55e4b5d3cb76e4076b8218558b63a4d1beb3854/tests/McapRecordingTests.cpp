#include "sim/Simulation.hpp"
#include "sim/control/FlightControlManager.hpp"
#include "sim/control/FlightControlMode.hpp"
#include "sim/gnc/autopilot/IRollHoldAutopilot.hpp"
#include "sim/gnc/autopilot/MyAutopilot.hpp"
#include "sim/gnc/autopilot/PX4Autopilot.hpp"
#include "sim/telemetry/recording/TelemetryRecordingService.hpp"
#include "contract/telemetry/TelemetryTime.hpp"
#include "contract/telemetry/mcap/McapRecordingReader.hpp"
#include "contract/telemetry/mcap/McapTelemetryRecorder.hpp"
#include "contract/telemetry/mcap/McapTelemetrySchema.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef JSB_TEST_SAMPLE_MCAP_PATH
#define JSB_TEST_SAMPLE_MCAP_PATH "telemetry.mcap"
#endif

namespace {
using telemetry::recording::AircraftRollState;
using telemetry::recording::ControlInputState;
using telemetry::recording::McapRecordingReader;
using telemetry::recording::McapTelemetryRecorder;
using telemetry::recording::RecordedSample;
using telemetry::recording::RecordingMetadata;
using telemetry::recording::RecordingState;
using telemetry::recording::RollHoldDiagnostics;
using telemetry::recording::TelemetryFrame;
using telemetry::recording::TelemetryRecordingConfig;
using telemetry::recording::TelemetrySourceFrame;

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path()
            / ("jsb-test-mcap-" + std::to_string(suffix));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &GetPath() const { return path_; }

private:
  std::filesystem::path path_;
};

RecordingMetadata MakeMetadata() {
  return RecordingMetadata{
      .contractVersion = "2.0.0",
      .telemetrySchemaVersion = 1,
      .applicationVersion = "test-version",
      .gitCommit = "test-commit",
      .runtimeBranch = "backend",
      .aircraft = "c172x",
      .scenarioName = "roll_hold_5deg",
      .scenarioFile = "scenarios/roll_hold_5deg_30s.yaml",
      .scenarioSchemaVersion = 1,
      .scenarioType = "roll_hold",
      .scenarioDurationSec = 30.0,
      .simulationDtSec = 0.01,
      .executionVariant = "primary",
      .primaryAutopilot = "MyAutopilot",
      .baselineAutopilot = "PX4Autopilot",
  };
}

TelemetrySourceFrame MakeSource(double offset) {
  return TelemetrySourceFrame{
      .rollHold =
          RollHoldDiagnostics{
              .commandedRollRad = 0.1 + offset,
              .rollRad = 0.08 + offset,
              .rollErrorRad = 0.02,
              .commandedRollRateRadPerSec = 0.2 + offset,
              .rollRateRadPerSec = 0.15 + offset,
              .rollRateErrorRadPerSec = 0.05,
              .aileronCommand = 0.12 + offset,
          },
      .aircraftState = AircraftRollState{0.08 + offset, 0.15 + offset},
      .controlInput = ControlInputState{0.11 + offset},
  };
}

TelemetryFrame MakeFrame(double timeSec) {
  return TelemetryFrame{
      .simulationTimeSec = timeSec,
      .primary = MakeSource(0.0),
      .baseline = MakeSource(1.0),
  };
}

void TestTimestampConversion() {
  using telemetry::recording::SimulationTimeToNanoseconds;
  Require(SimulationTimeToNanoseconds(0.0) == 0,
      "zero simulation time conversion failed");
  Require(SimulationTimeToNanoseconds(0.01) == 10'000'000,
      "0.01 second conversion failed");
  Require(SimulationTimeToNanoseconds(0.02) == 20'000'000,
      "0.02 second conversion failed");
  Require(!SimulationTimeToNanoseconds(-0.01),
      "negative simulation time was accepted");
  Require(!SimulationTimeToNanoseconds(std::numeric_limits<double>::infinity()),
      "infinite simulation time was accepted");
  Require(
      !SimulationTimeToNanoseconds(std::numeric_limits<double>::quiet_NaN()),
      "NaN simulation time was accepted");
  Require(!SimulationTimeToNanoseconds(std::numeric_limits<double>::max()),
      "overflowing simulation time was accepted");
  Require(
      telemetry::recording::TelemetryRecordingService::SanitizeRecordingName(
          "roll: hold/5?")
          == "roll_hold_5",
      "recording filename sanitization failed");
}

void TestRoundTripMultipleChannelsAndMetadata() {
  TemporaryDirectory temporary;
  const std::filesystem::path path = temporary.GetPath() / "round_trip.mcap";
  McapTelemetryRecorder recorder;
  Require(recorder.Start(TelemetryRecordingConfig{.outputPath = path},
              MakeMetadata()),
      "recorder failed to start");
  recorder.Record(MakeFrame(0.0));
  recorder.Record(MakeFrame(0.01));
  recorder.Record(MakeFrame(0.02));
  recorder.RecordScenarioEvent({
      .simulationTimeSec = 0.01,
      .type = "roll_command_changed",
      .targetRollRad = 0.1,
  });
  recorder.RecordPrimarySettings({
      .simulationTimeSec = 0.01,
      .rollAngleProportionalGain = 1.1,
      .rollRateProportionalGain = 2.2,
  });
  recorder.RecordBaselineSettings({
      .simulationTimeSec = 0.01,
      .rollTimeConstantSec = 0.35,
      .maximumRollRateRadPerSec = 1.2,
      .rateProportionalGain = 0.16,
      .rateIntegralGain = 0.08,
      .rateDerivativeGain = 0.0,
      .rateFeedForwardGain = 0.8,
      .integratorLimit = 0.15,
  });
  recorder.Stop();

  const auto status = recorder.GetStatus();
  Require(status.state == RecordingState::Idle,
      "recorder did not return to Idle after Stop");
  Require(status.stats.messagesWritten == 15,
      "unexpected recorded message count");
  Require(status.stats.bytesWritten > 0, "recorded byte count is empty");

  McapRecordingReader reader;
  Require(reader.Open(path),
      "reader failed to open round-trip file: " + reader.GetLastError());
  const auto &run = reader.GetRunInfo();
  Require(run.scenarioName == "roll_hold_5deg",
      "scenario_name metadata mismatch");
  Require(run.contractVersion == "2.0.0", "contract_version metadata mismatch");
  Require(run.executionVariant == "primary",
      "execution_variant metadata mismatch");
  Require(run.telemetrySchemaVersion == 1,
      "telemetry_schema_version metadata mismatch");
  Require(run.scenarioSchemaVersion == 1,
      "scenario_schema_version metadata mismatch");
  Require(run.scenarioType == "roll_hold", "scenario_type metadata mismatch");
  Require(run.runtimeBranch == "backend", "runtime_branch metadata mismatch");
  Require(run.gitCommit == "test-commit", "git_commit metadata mismatch");
  Require(run.aircraft == "c172x", "aircraft metadata mismatch");
  Require(run.simulationDtSec == 0.01, "simulation_dt_sec metadata mismatch");
  Require(run.startTimeSec == 0.0 && run.endTimeSec == 0.02,
      "recording time range mismatch");

  const std::vector<RecordedSample> primary =
      reader.ReadMessages("/jsb/primary/control/roll");
  const std::vector<RecordedSample> baseline =
      reader.ReadMessages("/jsb/baseline/control/roll");
  Require(primary.size() == 3, "primary topic filter mismatch");
  Require(baseline.size() == 3, "baseline topic filter mismatch");
  Require(reader.ReadMessages("/primary/roll_hold/settings").size() == 1,
      "primary settings were not recorded event-wise");
  Require(reader.ReadMessages("/baseline/roll_hold/settings").size() == 1,
      "baseline settings were not recorded event-wise");
  const std::array<std::uint64_t, 3> expectedTimes{0, 10'000'000, 20'000'000};
  for (std::size_t index = 0; index < primary.size(); ++index) {
    Require(primary[index].logTimeNanoseconds == expectedTimes[index],
        "log timestamp mismatch");
    Require(primary[index].publishTimeNanoseconds == expectedTimes[index],
        "publish timestamp mismatch");
  }

  const auto decoded =
      telemetry::recording::mcap_schema::DeserializeRollHoldDiagnostics(
          primary.front().payload);
  Require(decoded.has_value(), "roll-hold Protobuf deserialization failed");
  Require(*decoded == *MakeSource(0.0).rollHold,
      "roll-hold field round-trip mismatch");

  const auto ranged = reader.ReadMessages("/jsb/primary/control/roll",
      telemetry::recording::RecordedTimeRange{0.01, 0.01});
  Require(ranged.size() == 1 && ranged.front().logTimeNanoseconds == 10'000'000,
      "inclusive time range query mismatch");

  std::ifstream file(path, std::ios::binary);
  std::array<unsigned char, 8> magic{};
  file.read(reinterpret_cast<char *>(magic.data()), magic.size());
  const std::array<unsigned char, 8>
      expectedMagic{0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};
  Require(magic == expectedMagic, "file does not have standard MCAP magic");
}

void TestLifecycleEmptyAndFailureHandling() {
  TemporaryDirectory temporary;
  McapTelemetryRecorder recorder;

  const std::filesystem::path emptyPath = temporary.GetPath() / "empty.mcap";
  Require(recorder.Start(TelemetryRecordingConfig{.outputPath = emptyPath},
              MakeMetadata()),
      "empty recording failed to start");
  recorder.Stop();
  McapRecordingReader emptyReader;
  Require(emptyReader.Open(emptyPath),
      "empty MCAP failed to open: " + emptyReader.GetLastError());
  Require(emptyReader.ReadMessages().empty(),
      "empty recording unexpectedly contains messages");

  const std::filesystem::path secondPath =
      temporary.GetPath() / "second_session.mcap";
  Require(recorder.Start(TelemetryRecordingConfig{.outputPath = secondPath},
              MakeMetadata()),
      "recorder could not start a second session");
  recorder.Record(MakeFrame(0.0));
  recorder.Stop();
  McapRecordingReader secondReader;
  Require(secondReader.Open(secondPath), "second session failed to open");
  Require(!secondReader.ReadMessages().empty(),
      "second session contains no messages");

  const std::filesystem::path blocker = temporary.GetPath() / "blocker";
  {
    std::ofstream file(blocker);
    file << "not a directory";
  }
  Require(!recorder.Start(
              TelemetryRecordingConfig{.outputPath = blocker / "bad.mcap"},
              MakeMetadata()),
      "invalid path unexpectedly started recording");
  Require(recorder.GetStatus().state == RecordingState::Error,
      "invalid path did not transition recorder to Error");

  const std::filesystem::path recoveredPath =
      temporary.GetPath() / "recovered.mcap";
  Require(recorder.Start(TelemetryRecordingConfig{.outputPath = recoveredPath},
              MakeMetadata()),
      "recorder did not recover from Error on a new Start");
  TelemetryFrame invalidFrame = MakeFrame(0.0);
  invalidFrame.primary->rollHold->rollRad =
      std::numeric_limits<double>::quiet_NaN();
  recorder.Record(invalidFrame);
  Require(recorder.GetStatus().state == RecordingState::Recording,
      "serialization failure stopped the recording session");
  Require(recorder.GetStatus().stats.serializationErrors == 1,
      "serialization failure was not counted");
  recorder.Stop();
}

void EnableRollHold(sim::Simulation &simulation, double targetRollRad) {
  auto *manager = simulation.GetComponent<control::FlightControlManager>();
  Require(manager != nullptr, "flight control manager is missing");
  auto *rollHold =
      dynamic_cast<gnc::IRollHoldAutopilot *>(&manager->GetAutopilot());
  Require(rollHold != nullptr, "roll-hold capability is missing");
  rollHold->SetTargetRollRad(targetRollRad);
  rollHold->SetRollHoldEnabled(true);
  manager->SetMode(control::FlightControlMode::Autopilot);
}

void TestSimulationIntegrationAndWriteSample() {
  const std::filesystem::path samplePath = JSB_TEST_SAMPLE_MCAP_PATH;
  std::filesystem::create_directories(samplePath.parent_path());

  sim::SimulationConfig config;
  config.simulationHz = 100.0;
  sim::Simulation primary(std::make_unique<gnc::MyAutopilot>());
  sim::Simulation baseline(std::make_unique<gnc::PX4Autopilot>());
  Require(primary.Initialize(config),
      "primary simulation failed to initialize");
  Require(baseline.Initialize(config),
      "baseline simulation failed to initialize");
  EnableRollHold(primary, 0.08726646259971647);
  EnableRollHold(baseline, 0.08726646259971647);

  telemetry::recording::TelemetryRecordingService service;
  Require(service.Start(TelemetryRecordingConfig{.outputPath = samplePath},
              MakeMetadata()),
      "integration recorder failed to start");
  service.RecordScenarioEvent({
      .simulationTimeSec = 0.0,
      .type = "scenario_start",
      .targetRollRad = 0.08726646259971647,
  });
  service.RecordPrimarySettings({
      .simulationTimeSec = 0.0,
      .rollAngleProportionalGain = 0.0,
      .rollRateProportionalGain = 0.0,
  });
  service.RecordBaselineSettings({
      .simulationTimeSec = 0.0,
      .rollTimeConstantSec = 0.35,
      .maximumRollRateRadPerSec = 1.2217304763960306,
      .rateProportionalGain = 0.16,
      .rateIntegralGain = 0.08,
      .rateDerivativeGain = 0.0,
      .rateFeedForwardGain = 0.8,
      .integratorLimit = 0.15,
  });
  for (int tick = 0; tick < 4; ++tick) {
    Require(primary.Step(config.GetDT()), "primary simulation step failed");
    Require(baseline.Step(config.GetDT()), "baseline simulation step failed");
    service.Consume(primary.GetTime(),
        primary.GetTelemetryRegistry(),
        &baseline.GetTelemetryRegistry());
  }
  service.Stop();
  primary.Shutdown();
  baseline.Shutdown();

  McapRecordingReader reader;
  Require(reader.Open(samplePath),
      "integration sample failed to open: " + reader.GetLastError());
  const auto primaryRoll = reader.ReadMessages("/jsb/primary/control/roll");
  const auto baselineRoll = reader.ReadMessages("/jsb/baseline/control/roll");
  Require(!primaryRoll.empty(),
      "integration sample has no primary roll/aileron data");
  Require(!baselineRoll.empty(),
      "integration sample has no baseline PX4 roll/aileron data");
  Require(telemetry::recording::mcap_schema::DeserializeRollHoldDiagnostics(
              primaryRoll.front().payload)
              .has_value(),
      "integration roll-hold payload is invalid");
}
} // namespace

int main() {
  TestTimestampConversion();
  TestRoundTripMultipleChannelsAndMetadata();
  TestLifecycleEmptyAndFailureHandling();
  TestSimulationIntegrationAndWriteSample();
  return 0;
}
