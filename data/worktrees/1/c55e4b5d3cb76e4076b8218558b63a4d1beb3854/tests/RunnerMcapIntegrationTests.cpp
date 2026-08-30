#include "runner/McapRunObserver.hpp"
#include "runner/SimulationRunner.hpp"

#include "contract/telemetry/mcap/McapRecordingReader.hpp"
#include "contract/telemetry/mcap/McapTelemetrySchema.hpp"
#include "sim/scenario/SimulationScenarioSerializer.hpp"
#include "sim/runtime/SimulationComparison.hpp"
#include "sim/runtime/SimulationRuntime.hpp"
#include "telemetry/simulation.pb.h"

#include <google/protobuf/descriptor.pb.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef JSB_TEST_CANONICAL_SCENARIO_PATH
#define JSB_TEST_CANONICAL_SCENARIO_PATH "scenarios/roll_hold_5deg_30s.yaml"
#endif

namespace {
using runner::McapRunObserver;
using runner::RunnerExitCode;
using runner::RunnerOptions;
using runner::RunnerResult;
using runner::SimulationRunner;
using telemetry::recording::McapRecordingReader;
using telemetry::recording::RecordedChannelInfo;
using telemetry::recording::RecordedSample;

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
            / ("jsb-runner-mcap-test-" + std::to_string(suffix));
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

std::string ReadTextFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.is_open(), "failed to open " + path.string());
  return std::string(std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

RunnerOptions MakeOptions(const std::filesystem::path &output) {
  RunnerOptions options;
  options.scenarioPath = JSB_TEST_HEADLESS_SCENARIO_PATH;
  options.outputDirectory = output;
  return options;
}

RunnerResult RunWithMcap(const RunnerOptions &options,
    const volatile std::sig_atomic_t *running = nullptr) {
  McapRunObserver observer;
  SimulationRunner runner;
  runner.AddObserver(observer);
  return runner.Run(options, running);
}

void RequireMonotonicSimulationTimestamps(
    const std::vector<RecordedSample> &samples, std::string_view topic) {
  Require(!samples.empty(), std::string(topic) + " has no samples");
  for (std::size_t index = 0; index < samples.size(); ++index) {
    Require(samples[index].logTimeNanoseconds
                == samples[index].publishTimeNanoseconds,
        std::string(topic) + " log/publish timestamps differ");
    if (index > 0) {
      Require(samples[index - 1].logTimeNanoseconds
                  < samples[index].logTimeNanoseconds,
          std::string(topic) + " timestamps are not strictly monotonic");
    }
  }
}

void RequireSameSamples(const std::vector<RecordedSample> &first,
    const std::vector<RecordedSample> &second, std::string_view topic) {
  Require(first.size() == second.size(),
      std::string(topic) + " sample count is not deterministic");
  for (std::size_t index = 0; index < first.size(); ++index) {
    Require(first[index].logTimeNanoseconds == second[index].logTimeNanoseconds
                && first[index].publishTimeNanoseconds
                       == second[index].publishTimeNanoseconds
                && first[index].payload == second[index].payload,
        std::string(topic) + " samples are not deterministic");
  }
}

const RecordedChannelInfo &FindChannel(const McapRecordingReader &reader,
    std::string_view topic) {
  for (const RecordedChannelInfo &channel : reader.GetChannels()) {
    if (channel.topic == topic) {
      return channel;
    }
  }
  throw std::runtime_error("missing MCAP channel " + std::string(topic));
}

void TestSuccessfulRunArtifactsAndSignals() {
  TemporaryDirectory temporary;
  const std::filesystem::path output = temporary.GetPath() / "completed";
  const RunnerResult result = RunWithMcap(MakeOptions(output));
  Require(result.exitCode == RunnerExitCode::Success,
      "runner failed: " + result.error);
  Require(result.status == "completed", "runner status is not completed");
  Require(result.steps == 10, "runner produced an unexpected step count");

  const std::filesystem::path manifestPath = output / "run.json";
  const std::filesystem::path mcapPath = output / "telemetry.mcap";
  const std::filesystem::path scenarioSnapshotPath = output / "scenario.yaml";
  Require(std::filesystem::is_regular_file(manifestPath),
      "run.json was not produced");
  Require(std::filesystem::is_regular_file(mcapPath),
      "telemetry.mcap was not produced");
  Require(std::filesystem::is_regular_file(scenarioSnapshotPath),
      "scenario.yaml was not produced");
  Require(ReadTextFile(scenarioSnapshotPath)
              == ReadTextFile(JSB_TEST_HEADLESS_SCENARIO_PATH),
      "scenario snapshot does not preserve the exact input bytes");

  const std::string manifest = ReadTextFile(manifestPath);
  Require(manifest.find("\"status\": \"completed\"") != std::string::npos,
      "run.json status is not completed");
  Require(manifest.find("\"variants\": [\"baseline\", \"primary\"]")
              != std::string::npos,
      "run.json dual execution provenance is missing");
  Require(manifest.find("\"autopilot\"") == std::string::npos
              && manifest.find("\"variant\":") == std::string::npos,
      "run.json contains a legacy single-variant selector");

  McapRecordingReader reader;
  Require(reader.Open(mcapPath),
      "failed to open runner MCAP: " + reader.GetLastError());
  Require(reader.GetRunInfo().scenarioName == "Headless Smoke",
      "runner MCAP scenario metadata is incorrect");
  Require(reader.GetRunInfo().simulationDtSec == 0.01,
      "runner MCAP timestep metadata is incorrect");
  Require(reader.GetRunInfo().contractVersion == "2.0.0",
      "runner MCAP contract version is incorrect");
  Require(reader.GetRunInfo().executionMode == "compare"
              && reader.GetRunInfo().executionVariants == "baseline,primary",
      "runner MCAP dual execution provenance is incorrect");
  Require(reader.GetRunInfo().telemetrySchemaVersion == 1,
      "runner MCAP telemetry schema version is incorrect");
  Require(reader.GetRunInfo().gitCommit.size() == 40,
      "runner MCAP does not contain the immutable full commit SHA");
  Require(reader.GetRunInfo().scenarioDigest.size() == 64,
      "runner MCAP does not contain the scenario SHA-256 digest");

  const std::vector<RecordedSample> diagnostics =
      reader.ReadMessages("/jsb/primary/control/roll");
  const std::vector<RecordedSample> baselineDiagnostics =
      reader.ReadMessages("/jsb/baseline/control/roll");
  const std::vector<RecordedSample> aircraft =
      reader.ReadMessages("/jsb/primary/aircraft/state");
  const std::vector<RecordedSample> simulationEvents =
      reader.ReadMessages("/jsb/simulation/event");
  RequireMonotonicSimulationTimestamps(diagnostics,
      "/jsb/primary/control/roll");
  RequireMonotonicSimulationTimestamps(baselineDiagnostics,
      "/jsb/baseline/control/roll");
  Require(baselineDiagnostics.size() == diagnostics.size(),
      "baseline and primary sample counts differ");
  for (std::size_t index = 0; index < diagnostics.size(); ++index) {
    Require(baselineDiagnostics[index].logTimeNanoseconds
                == diagnostics[index].logTimeNanoseconds,
        "baseline and primary timestamps are not aligned");
  }
  RequireMonotonicSimulationTimestamps(aircraft, "/jsb/primary/aircraft/state");
  Require(!simulationEvents.empty(), "/jsb/simulation/event has no samples");
  bool foundScheduledCommand = false;
  for (const RecordedSample &sample : simulationEvents) {
    jsb::telemetry::v1::SimulationEvent event;
    Require(event.ParseFromString(sample.payload),
        "runner simulation event payload is invalid");
    if (event.type() == jsb::telemetry::v1::SIMULATION_EVENT_COMMAND_APPLIED) {
      foundScheduledCommand = true;
      Require(event.sim_time_ns() == 20'000'000,
          "scenario command did not execute at its declared simulation time");
      Require(event.has_target_roll_rad(),
          "scenario command event omitted the resolved roll target");
    }
  }
  Require(foundScheduledCommand,
      "runner MCAP did not record the scheduled scenario command");
  Require(diagnostics.front().logTimeNanoseconds == 10'000'000,
      "runner MCAP does not start at the first simulation step");
  Require(diagnostics.back().logTimeNanoseconds == 100'000'000,
      "runner MCAP simulation time range is incorrect");

  Require(telemetry::recording::mcap_schema::DeserializeRollHoldDiagnostics(
              diagnostics.front().payload)
              .has_value(),
      "runner roll-hold payload is invalid");
  for (const auto &channel : reader.GetChannels()) {
    if (channel.topic == "/jsb/primary/control/roll"
        || channel.topic == "/jsb/baseline/control/roll"
        || channel.topic == "/jsb/primary/aircraft/state"
        || channel.topic == "/jsb/simulation/event") {
      Require(channel.messageEncoding == "protobuf",
          "contract channel message encoding is not protobuf");
      Require(channel.schemaEncoding == "protobuf",
          "contract channel schema encoding is not protobuf");
      Require(channel.schemaDataSize > 0,
          "contract channel does not embed a FileDescriptorSet");
      google::protobuf::FileDescriptorSet descriptorSet;
      Require(descriptorSet.ParseFromString(channel.schemaData),
          "contract channel schema is not a valid FileDescriptorSet");
      Require(descriptorSet.file_size() > 0,
          "contract channel FileDescriptorSet is empty");
    }
  }

  const std::filesystem::path repeatedOutput = temporary.GetPath() / "repeated";
  const RunnerResult repeatedResult = RunWithMcap(MakeOptions(repeatedOutput));
  Require(repeatedResult.exitCode == RunnerExitCode::Success,
      "repeated deterministic run failed: " + repeatedResult.error);
  McapRecordingReader repeatedReader;
  Require(repeatedReader.Open(repeatedOutput / "telemetry.mcap"),
      "failed to open repeated runner MCAP: " + repeatedReader.GetLastError());
  RequireSameSamples(diagnostics,
      repeatedReader.ReadMessages("/jsb/primary/control/roll"),
      "/jsb/primary/control/roll");
  RequireSameSamples(baselineDiagnostics,
      repeatedReader.ReadMessages("/jsb/baseline/control/roll"),
      "/jsb/baseline/control/roll");
  RequireSameSamples(aircraft,
      repeatedReader.ReadMessages("/jsb/primary/aircraft/state"),
      "/jsb/primary/aircraft/state");
  RequireSameSamples(simulationEvents,
      repeatedReader.ReadMessages("/jsb/simulation/event"),
      "/jsb/simulation/event");
}

void TestFailureManifestAndMcapOpenFailure() {
  TemporaryDirectory temporary;

  RunnerOptions invalidScenario = MakeOptions(temporary.GetPath() / "invalid");
  invalidScenario.scenarioPath = temporary.GetPath() / "missing.yaml";
  const RunnerResult scenarioResult = RunWithMcap(invalidScenario);
  Require(scenarioResult.exitCode == RunnerExitCode::ScenarioLoadFailure,
      "missing scenario returned an unexpected exit code");
  Require(std::filesystem::is_regular_file(
              invalidScenario.outputDirectory / "run.json"),
      "missing scenario did not produce run.json");

  const std::filesystem::path blockedOutput = temporary.GetPath() / "blocked";
  std::filesystem::create_directories(blockedOutput / "telemetry.mcap");
  const RunnerResult recorderResult = RunWithMcap(MakeOptions(blockedOutput));
  Require(recorderResult.exitCode == RunnerExitCode::OutputFailure,
      "MCAP open failure returned an unexpected exit code");
  Require(recorderResult.error.find("failed to initialize MCAP recorder")
              != std::string::npos,
      "MCAP open failure did not propagate through RunnerResult.error");
  const std::string manifest = ReadTextFile(blockedOutput / "run.json");
  Require(manifest.find("\"status\": \"failed\"") != std::string::npos,
      "MCAP open failure manifest is not failed");

  Require(recorderResult.baselineStatus == "failed"
              && recorderResult.primaryStatus == "failed",
      "MCAP failure did not stop both variants");
}

void TestInterruptedRunFinalizesMcap() {
  TemporaryDirectory temporary;
  const std::filesystem::path output = temporary.GetPath() / "interrupted";
  volatile std::sig_atomic_t running = 0;
  const RunnerResult result = RunWithMcap(MakeOptions(output), &running);
  Require(result.exitCode != RunnerExitCode::Success,
      "interrupted runner unexpectedly succeeded");
  Require(result.status == "interrupted", "runner did not report interruption");

  McapRecordingReader reader;
  Require(reader.Open(output / "telemetry.mcap"),
      "interrupted MCAP was not finalized: " + reader.GetLastError());
  Require(std::filesystem::is_regular_file(output / "run.json"),
      "interrupted run did not produce run.json");
}

void TestComparisonExecutionArtifactsAndSynchronization() {
  TemporaryDirectory temporary;
  RunnerOptions options = MakeOptions(temporary.GetPath() / "comparison");
  options.scenarioPath = JSB_TEST_CANONICAL_SCENARIO_PATH;

  const RunnerResult result = RunWithMcap(options);
  Require(result.exitCode == RunnerExitCode::Success,
      "comparison execution failed: " + result.error);
  Require(result.steps == 900 && result.simulationTimeSec == 30.0,
      "comparison did not use the shared scenario horizon");
  Require(result.baselineStatus == "completed"
              && result.primaryStatus == "completed",
      "comparison variant results are not completed");

  const std::string manifest =
      ReadTextFile(options.outputDirectory / "run.json");
  Require(manifest.find("\"mode\": \"compare\"") != std::string::npos,
      "comparison metadata mode is missing");
  Require(manifest.find("\"variants\": [\"baseline\", \"primary\"]")
              != std::string::npos,
      "comparison metadata variants are missing");
  Require(manifest.find("\"baseline\": {\"status\": \"completed\"}")
                  != std::string::npos
              && manifest.find("\"primary\": {\"status\": \"completed\"}")
                     != std::string::npos,
      "comparison metadata results are missing");
  Require(manifest.find("\"autopilot\"") == std::string::npos,
      "comparison metadata contains a single autopilot selector");

  McapRecordingReader reader;
  Require(reader.Open(options.outputDirectory / "telemetry.mcap"),
      "failed to open comparison MCAP: " + reader.GetLastError());
  Require(reader.GetRunInfo().executionMode == "compare",
      "comparison MCAP mode is incorrect");
  Require(reader.GetRunInfo().executionVariants == "baseline,primary",
      "comparison MCAP variants are incorrect");
  Require(reader.GetRunInfo().executionVariant.empty()
              && reader.GetRunInfo().resolvedAutopilot.empty(),
      "comparison MCAP contains an ambiguous single-variant selector");

  const auto baseline = reader.ReadMessages("/jsb/baseline/control/roll");
  const auto primary = reader.ReadMessages("/jsb/primary/control/roll");
  const auto baselineAircraft =
      reader.ReadMessages("/jsb/baseline/aircraft/state");
  const auto primaryAircraft =
      reader.ReadMessages("/jsb/primary/aircraft/state");
  Require(baseline.size() == 900 && primary.size() == 900,
      "comparison MCAP does not contain both complete trajectories");
  Require(baselineAircraft.size() == 900 && primaryAircraft.size() == 900,
      "comparison MCAP does not contain both aircraft trajectories");
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    Require(baseline[index].logTimeNanoseconds
                == primary[index].logTimeNanoseconds,
        "variant timestamps differ at the same shared step");
    const auto baselineState =
        telemetry::recording::mcap_schema::DeserializeRollHoldDiagnostics(
            baseline[index].payload);
    const auto primaryState =
        telemetry::recording::mcap_schema::DeserializeRollHoldDiagnostics(
            primary[index].payload);
    Require(baselineState && primaryState,
        "comparison control channel is not decodable with the common schema");
    Require(baselineState->commandedRollRad == primaryState->commandedRollRad,
        "variant commanded-roll schedules differ");
    Require(baselineAircraft[index].logTimeNanoseconds
                == primaryAircraft[index].logTimeNanoseconds,
        "variant aircraft timestamps differ at the same shared step");
  }
  const RecordedChannelInfo &baselineControlChannel =
      FindChannel(reader, "/jsb/baseline/control/roll");
  const RecordedChannelInfo &primaryControlChannel =
      FindChannel(reader, "/jsb/primary/control/roll");
  Require(baselineControlChannel.schemaName == primaryControlChannel.schemaName
              && baselineControlChannel.schemaData
                     == primaryControlChannel.schemaData,
      "variant control channels do not use the same protobuf schema");
  const RecordedChannelInfo &baselineAircraftChannel =
      FindChannel(reader, "/jsb/baseline/aircraft/state");
  const RecordedChannelInfo &primaryAircraftChannel =
      FindChannel(reader, "/jsb/primary/aircraft/state");
  Require(baselineAircraftChannel.schemaName
                  == primaryAircraftChannel.schemaName
              && baselineAircraftChannel.schemaData
                     == primaryAircraftChannel.schemaData,
      "variant aircraft channels do not use the same protobuf schema");
  Require(
      telemetry::recording::mcap_schema::DeserializeRollHoldDiagnostics(
          baseline.front().payload)
              ->rollRad
          == telemetry::recording::mcap_schema::DeserializeRollHoldDiagnostics(
              primary.front().payload)
              ->rollRad,
      "variant initial roll states differ");

  bool foundCommandAtFiveSeconds = false;
  for (const RecordedSample &sample :
      reader.ReadMessages("/jsb/simulation/event")) {
    jsb::telemetry::v1::SimulationEvent event;
    Require(event.ParseFromString(sample.payload),
        "comparison scenario event is not decodable");
    if (event.type() == jsb::telemetry::v1::SIMULATION_EVENT_COMMAND_APPLIED) {
      Require(event.sim_time_ns() == 5'000'000'000ULL,
          "comparison command did not fire at exactly 5 simulation seconds");
      foundCommandAtFiveSeconds = true;
    }
  }
  Require(foundCommandAtFiveSeconds,
      "comparison MCAP is missing the shared 5-second command event");
}

void TestLegacyVariantIsRejected() {
  TemporaryDirectory temporary;
  const std::string legacyYaml =
      "autopilot: baseline\n" + ReadTextFile(JSB_TEST_HEADLESS_SCENARIO_PATH);
  const std::filesystem::path legacyPath = temporary.GetPath() / "legacy.yaml";
  {
    std::ofstream output(legacyPath, std::ios::binary);
    output << legacyYaml;
  }
  RunnerOptions options = MakeOptions(temporary.GetPath() / "legacy");
  options.scenarioPath = legacyPath;
  const RunnerResult result = RunWithMcap(options);
  Require(result.exitCode == RunnerExitCode::ScenarioLoadFailure,
      "legacy variant was not rejected");
  Require(result.error.find("baseline and primary are always run together")
              != std::string::npos,
      "legacy variant rejection is not actionable");
  Require(!std::filesystem::exists(options.outputDirectory / "telemetry.mcap"),
      "legacy variant started simulation telemetry");
}

void TestDualExecutionCliParsing() {
  const runner::RunnerParseResult parsed = runner::ParseRunnerOptions(
      {"--scenario", "scenario.yaml", "--output", "out"});
  Require(parsed.options.has_value(), "canonical dual-run CLI did not parse");
  for (const std::vector<std::string_view> legacyOption : {
           std::vector<std::string_view>{"--variant", "baseline"},
           std::vector<std::string_view>{"--mode", "compare"},
       }) {
    std::vector<std::string_view> arguments = {"--scenario",
        "scenario.yaml",
        "--output",
        "out"};
    arguments.insert(arguments.end(), legacyOption.begin(), legacyOption.end());
    const runner::RunnerParseResult rejected =
        runner::ParseRunnerOptions(arguments);
    Require(!rejected.options.has_value()
                && rejected.error.find("always runs baseline and primary")
                       != std::string::npos,
        "legacy headless selection option was accepted");
  }
}

void TestSemanticCliOverridesAreRejected() {
  const std::vector<std::vector<std::string_view>> overrides = {
      {"--autopilot", "baseline"},
      {"--aircraft", "c172x"},
      {"--duration", "5"},
      {"--dt", "0.01"},
      {"--no-trim"},
  };
  for (const auto &override : overrides) {
    std::vector<std::string_view> arguments = {"--scenario",
        "primary.yaml",
        "--output",
        "out"};
    arguments.insert(arguments.end(), override.begin(), override.end());
    const runner::RunnerParseResult parsed =
        runner::ParseRunnerOptions(arguments);
    Require(!parsed.options.has_value(),
        "semantic CLI override was unexpectedly accepted");
    Require(parsed.error.find("defined by the scenario") != std::string::npos,
        "semantic CLI override rejection is not actionable");
  }
}

void TestInvalidScenarioFailsBeforeSimulationStarts() {
  TemporaryDirectory temporary;
  std::string yaml = ReadTextFile(JSB_TEST_HEADLESS_SCENARIO_PATH);
  const std::string validDuration = "duration_sec: 0.1";
  const std::size_t position = yaml.find(validDuration);
  Require(position != std::string::npos,
      "headless fixture duration was not found");
  yaml.replace(position, validDuration.size(), "duration_sec: 0");
  const std::filesystem::path scenarioPath =
      temporary.GetPath() / "invalid-duration.yaml";
  {
    std::ofstream output(scenarioPath, std::ios::binary);
    output << yaml;
  }
  RunnerOptions options = MakeOptions(temporary.GetPath() / "invalid-run");
  options.scenarioPath = scenarioPath;
  const RunnerResult result = RunWithMcap(options);
  Require(result.exitCode == RunnerExitCode::ScenarioLoadFailure,
      "invalid scenario returned an unexpected exit code");
  Require(result.steps == 0,
      "invalid scenario advanced the simulation before failing");
  Require(!std::filesystem::exists(options.outputDirectory / "telemetry.mcap"),
      "invalid scenario started telemetry recording");
}

void TestOneComparisonRuntimeInitializationFailure() {
  sim::SimulationScenario scenario;
  std::string error;
  Require(
      sim::SimulationScenarioSerializer::Load(JSB_TEST_HEADLESS_SCENARIO_PATH,
          scenario,
          error),
      "failed to load comparison initialization fixture");
  std::optional<sim::ExecutionVariant> failedVariant;
  const auto comparison = sim::SimulationComparison::Create(
      scenario,
      {},
      error,
      [](const sim::ResolvedExecutionSpec &execution,
          std::string &factoryError) {
        if (execution.variant == sim::ExecutionVariant::Primary) {
          factoryError = "injected primary creation failure";
          return std::unique_ptr<sim::SimulationRuntime>{};
        }
        return sim::SimulationRuntime::CreateForExecution(execution,
            factoryError);
      },
      &failedVariant);
  Require(comparison == nullptr,
      "comparison survived one runtime initialization failure");
  Require(error
              == "primary initialization failed: injected primary creation "
                 "failure",
      "comparison did not aggregate the variant initialization error");
  Require(failedVariant == sim::ExecutionVariant::Primary,
      "comparison did not identify the failed variant");
}

void TestOneVariantFailureStopsComparison() {
  sim::SimulationScenario scenario;
  std::string error;
  Require(
      sim::SimulationScenarioSerializer::Load(JSB_TEST_HEADLESS_SCENARIO_PATH,
          scenario,
          error),
      "failed to load variant-failure fixture");
  sim::SimulationRuntime *primaryRuntime = nullptr;
  auto comparison = sim::SimulationComparison::Create(scenario,
      {},
      error,
      [&](const sim::ResolvedExecutionSpec &execution,
          std::string &factoryError) {
        auto runtime =
            sim::SimulationRuntime::CreateForExecution(execution, factoryError);
        if (execution.variant == sim::ExecutionVariant::Primary) {
          primaryRuntime = runtime.get();
        }
        return runtime;
      });
  Require(comparison != nullptr && primaryRuntime != nullptr,
      "failed to create variant-failure comparison: " + error);

  primaryRuntime->Shutdown();
  Require(!comparison->Tick(),
      "comparison survived a fatal primary runtime clock failure");
  Require(comparison->GetState() == sim::ComparisonExecutionState::Failed,
      "comparison did not enter failed state");
  Require(
      comparison->GetVariantResult(sim::ExecutionVariant::Primary).state
              == sim::ComparisonExecutionState::Failed
          && comparison->GetVariantResult(sim::ExecutionVariant::Baseline).state
                 == sim::ComparisonExecutionState::Stopped,
      "comparison did not attribute the failure and stop its peer");
  Require(comparison->GetLastError().starts_with("primary:"),
      "comparison failure does not identify the primary variant");
  comparison->Shutdown();
}

void TestVariantParameterBoundary() {
  sim::SimulationScenario scenario;
  std::string error;
  Require(
      sim::SimulationScenarioSerializer::Load(JSB_TEST_HEADLESS_SCENARIO_PATH,
          scenario,
          error),
      "failed to load parameter-boundary fixture");
  bool sawBaselineParameters = false;
  bool sawPrimaryParameters = false;
  const sim::ComparisonExecutionRequest request{
      .scenario = scenario,
      .source = {},
      .baselineParameters = {{"baseline.gain", 1.0}},
      .primaryParameters = {{"primary.gain", 2.0}},
  };
  auto comparison = sim::SimulationComparison::Create(request,
      error,
      [&](const sim::ResolvedExecutionSpec &execution,
          std::string &factoryError) {
        if (execution.variant == sim::ExecutionVariant::Baseline) {
          sawBaselineParameters =
              execution.parameters == request.baselineParameters;
        } else {
          sawPrimaryParameters =
              execution.parameters == request.primaryParameters;
        }
        return sim::SimulationRuntime::CreateForExecution(execution,
            factoryError);
      });
  Require(comparison != nullptr, "parameterized comparison failed: " + error);
  Require(sawBaselineParameters && sawPrimaryParameters,
      "variant parameter sets did not reach runtime creation");
  comparison->Shutdown();
}
} // namespace

int main() {
  TestSuccessfulRunArtifactsAndSignals();
  TestFailureManifestAndMcapOpenFailure();
  TestInterruptedRunFinalizesMcap();
  TestComparisonExecutionArtifactsAndSynchronization();
  TestLegacyVariantIsRejected();
  TestDualExecutionCliParsing();
  TestSemanticCliOverridesAreRejected();
  TestInvalidScenarioFailsBeforeSimulationStarts();
  TestOneComparisonRuntimeInitializationFailure();
  TestOneVariantFailureStopsComparison();
  TestVariantParameterBoundary();
  return 0;
}
