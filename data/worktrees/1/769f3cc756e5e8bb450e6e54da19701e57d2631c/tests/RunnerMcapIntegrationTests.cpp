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
  options.variant = sim::ExecutionVariant::Primary;
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
  Require(manifest.find("\"autopilot\": \"primary\"") != std::string::npos,
      "run.json autopilot is missing");
  Require(manifest.find("\"variant\": \"primary\"") != std::string::npos,
      "run.json execution variant is missing");

  McapRecordingReader reader;
  Require(reader.Open(mcapPath),
      "failed to open runner MCAP: " + reader.GetLastError());
  Require(reader.GetRunInfo().scenarioName == "Headless Smoke",
      "runner MCAP scenario metadata is incorrect");
  Require(reader.GetRunInfo().simulationDtSec == 0.01,
      "runner MCAP timestep metadata is incorrect");
  Require(reader.GetRunInfo().contractVersion == "2.0.0",
      "runner MCAP contract version is incorrect");
  Require(reader.GetRunInfo().executionVariant == "primary",
      "runner MCAP execution variant is incorrect");
  Require(reader.GetRunInfo().telemetrySchemaVersion == 1,
      "runner MCAP telemetry schema version is incorrect");
  Require(reader.GetRunInfo().gitCommit.size() == 40,
      "runner MCAP does not contain the immutable full commit SHA");
  Require(reader.GetRunInfo().scenarioDigest.size() == 64,
      "runner MCAP does not contain the scenario SHA-256 digest");

  const std::vector<RecordedSample> diagnostics =
      reader.ReadMessages("/jsb/primary/control/roll");
  const std::vector<RecordedSample> aircraft =
      reader.ReadMessages("/jsb/primary/aircraft/state");
  const std::vector<RecordedSample> simulationEvents =
      reader.ReadMessages("/jsb/simulation/event");
  RequireMonotonicSimulationTimestamps(diagnostics,
      "/jsb/primary/control/roll");
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

  const std::filesystem::path blockedComparison =
      temporary.GetPath() / "blocked-comparison";
  std::filesystem::create_directories(blockedComparison / "telemetry.mcap");
  RunnerOptions comparisonOptions = MakeOptions(blockedComparison);
  comparisonOptions.mode = runner::ExecutionMode::Compare;
  comparisonOptions.variant.reset();
  const RunnerResult comparisonResult = RunWithMcap(comparisonOptions);
  Require(comparisonResult.exitCode == RunnerExitCode::OutputFailure,
      "comparison MCAP open failure returned an unexpected exit code");
  Require(comparisonResult.baselineStatus == "failed"
              && comparisonResult.primaryStatus == "failed",
      "comparison MCAP failure did not stop both variants");
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

void TestSameScenarioExecutesBothVariants() {
  TemporaryDirectory temporary;
  RunnerOptions baselineOptions =
      MakeOptions(temporary.GetPath() / "baseline-run");
  baselineOptions.variant = sim::ExecutionVariant::Baseline;
  RunnerOptions primaryOptions =
      MakeOptions(temporary.GetPath() / "primary-run");

  const RunnerResult baselineResult = RunWithMcap(baselineOptions);
  const RunnerResult primaryResult = RunWithMcap(primaryOptions);
  Require(baselineResult.exitCode == RunnerExitCode::Success,
      "baseline execution failed: " + baselineResult.error);
  Require(primaryResult.exitCode == RunnerExitCode::Success,
      "primary execution failed: " + primaryResult.error);

  const std::string baselineManifest =
      ReadTextFile(baselineOptions.outputDirectory / "run.json");
  const std::string primaryManifest =
      ReadTextFile(primaryOptions.outputDirectory / "run.json");
  Require(baselineManifest.find("\"variant\": \"baseline\"")
              != std::string::npos,
      "baseline metadata is missing its variant");
  Require(primaryManifest.find("\"variant\": \"primary\"") != std::string::npos,
      "primary metadata is missing its variant");

  McapRecordingReader baselineReader;
  McapRecordingReader primaryReader;
  Require(
      baselineReader.Open(baselineOptions.outputDirectory / "telemetry.mcap"),
      "failed to read baseline MCAP");
  Require(primaryReader.Open(primaryOptions.outputDirectory / "telemetry.mcap"),
      "failed to read primary MCAP");
  Require(baselineReader.GetRunInfo().executionVariant == "baseline",
      "baseline MCAP variant is incorrect");
  Require(primaryReader.GetRunInfo().executionVariant == "primary",
      "primary MCAP variant is incorrect");
  Require(!baselineReader.ReadMessages("/jsb/primary/control/roll").empty(),
      "baseline execution did not use the canonical single-run topic");
  Require(!primaryReader.ReadMessages("/jsb/primary/control/roll").empty(),
      "primary execution did not use the canonical single-run topic");
  Require(baselineReader.GetRunInfo().scenarioDigest
              == primaryReader.GetRunInfo().scenarioDigest,
      "the two variants did not use identical Scenario bytes");
}

void TestComparisonExecutionArtifactsAndSynchronization() {
  TemporaryDirectory temporary;
  RunnerOptions options = MakeOptions(temporary.GetPath() / "comparison");
  options.scenarioPath = JSB_TEST_CANONICAL_SCENARIO_PATH;
  options.mode = runner::ExecutionMode::Compare;
  options.variant.reset();

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
  Require(baseline.size() == 900 && primary.size() == 900,
      "comparison MCAP does not contain both complete trajectories");
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
  }
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

void TestLegacyVariantConflictIsRejected() {
  TemporaryDirectory temporary;
  const std::string legacyYaml =
      "autopilot: baseline\n" + ReadTextFile(JSB_TEST_HEADLESS_SCENARIO_PATH);
  const std::filesystem::path legacyPath = temporary.GetPath() / "legacy.yaml";
  {
    std::ofstream output(legacyPath, std::ios::binary);
    output << legacyYaml;
  }
  RunnerOptions options = MakeOptions(temporary.GetPath() / "conflict");
  options.scenarioPath = legacyPath;
  options.variant = sim::ExecutionVariant::Primary;
  const RunnerResult result = RunWithMcap(options);
  Require(result.exitCode == RunnerExitCode::ScenarioLoadFailure,
      "conflicting legacy variant was not rejected");
  Require(result.error
              == "Scenario autopilot 'baseline' conflicts with execution "
                 "variant 'primary'.",
      "legacy conflict error is not explicit");
  Require(!std::filesystem::exists(options.outputDirectory / "telemetry.mcap"),
      "legacy conflict started simulation telemetry");

  options.outputDirectory = temporary.GetPath() / "matching";
  options.variant = sim::ExecutionVariant::Baseline;
  const RunnerResult matching = RunWithMcap(options);
  Require(matching.exitCode == RunnerExitCode::Success,
      "matching legacy variant did not migrate: " + matching.error);
  Require(ReadTextFile(options.outputDirectory / "run.json")
                  .find("\"variant\": \"baseline\"")
              != std::string::npos,
      "matching legacy variant was not resolved into metadata");

  options.mode = runner::ExecutionMode::Compare;
  options.variant.reset();
  options.outputDirectory = temporary.GetPath() / "compare-legacy";
  const RunnerResult comparison = RunWithMcap(options);
  Require(comparison.exitCode == RunnerExitCode::ScenarioLoadFailure,
      "compare mode accepted a legacy autopilot selector");
  Require(comparison.error.find("cannot be used in compare mode")
              != std::string::npos,
      "compare legacy selector rejection is not actionable");
}

void TestVariantCliParsing() {
  const runner::RunnerParseResult baseline =
      runner::ParseRunnerOptions({"--scenario",
          "scenario.yaml",
          "--variant",
          "baseline",
          "--output",
          "out"});
  Require(baseline.options.has_value()
              && baseline.options->variant == sim::ExecutionVariant::Baseline,
      "baseline CLI variant did not parse");
  const runner::RunnerParseResult primary =
      runner::ParseRunnerOptions({"--scenario",
          "scenario.yaml",
          "--variant",
          "primary",
          "--output",
          "out"});
  Require(primary.options.has_value()
              && primary.options->variant == sim::ExecutionVariant::Primary,
      "primary CLI variant did not parse");
  for (std::string_view unsupported : {"foo", "Primary"}) {
    const runner::RunnerParseResult parsed =
        runner::ParseRunnerOptions({"--scenario",
            "scenario.yaml",
            "--variant",
            unsupported,
            "--output",
            "out"});
    Require(!parsed.options.has_value(),
        "unsupported CLI variant was accepted");
    Require(parsed.error
                == "Unsupported execution variant: " + std::string(unsupported),
        "unsupported CLI variant error is not explicit");
  }
  const runner::RunnerParseResult missing = runner::ParseRunnerOptions(
      {"--scenario", "scenario.yaml", "--output", "out"});
  Require(missing.error == "--variant is required in single mode",
      "missing CLI variant was not rejected");
  const runner::RunnerParseResult comparison = runner::ParseRunnerOptions(
      {"--scenario", "scenario.yaml", "--mode", "compare", "--output", "out"});
  Require(comparison.options.has_value()
              && comparison.options->mode == runner::ExecutionMode::Compare
              && !comparison.options->variant,
      "compare CLI mode did not parse");
  const runner::RunnerParseResult ambiguous =
      runner::ParseRunnerOptions({"--scenario",
          "scenario.yaml",
          "--mode",
          "compare",
          "--variant",
          "baseline",
          "--output",
          "out"});
  Require(ambiguous.error == "--variant must not be specified in compare mode",
      "compare mode did not reject an ambiguous variant");
  const runner::RunnerParseResult invalidMode = runner::ParseRunnerOptions(
      {"--scenario", "scenario.yaml", "--mode", "Compare", "--output", "out"});
  Require(invalidMode.error == "Unsupported execution mode: Compare",
      "unsupported execution mode error is not explicit");
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
        "--variant",
        "primary",
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
  const auto comparison = sim::SimulationComparison::Create(scenario,
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
      });
  Require(comparison == nullptr,
      "comparison survived one runtime initialization failure");
  Require(error
              == "primary initialization failed: injected primary creation "
                 "failure",
      "comparison did not aggregate the variant initialization error");
}
} // namespace

int main() {
  TestSuccessfulRunArtifactsAndSignals();
  TestFailureManifestAndMcapOpenFailure();
  TestInterruptedRunFinalizesMcap();
  TestSameScenarioExecutesBothVariants();
  TestComparisonExecutionArtifactsAndSynchronization();
  TestLegacyVariantConflictIsRejected();
  TestVariantCliParsing();
  TestSemanticCliOverridesAreRejected();
  TestInvalidScenarioFailsBeforeSimulationStarts();
  TestOneComparisonRuntimeInitializationFailure();
  return 0;
}
