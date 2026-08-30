#include "SimulationRunner.hpp"

#include "common/crypto/Sha256.hpp"
#include "sim/execution/ExecutionVariantResolver.hpp"
#include "sim/runtime/SimulationComparison.hpp"
#include "sim/runtime/SimulationRuntime.hpp"
#include "sim/scenario/SimulationScenarioSerializer.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>
#include <utility>

namespace runner {
namespace {
using Clock = std::chrono::steady_clock;

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

std::string JsonEscape(std::string_view value) {
  std::string escaped;
  for (const char character : value) {
    switch (character) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(character);
      break;
    }
  }
  return escaped;
}

bool PrepareOutputDirectory(const std::filesystem::path &directory,
    std::string &error) {
  if (directory.empty()) {
    error = "output directory is empty";
    return false;
  }
  std::error_code filesystemError;
  std::filesystem::create_directories(directory, filesystemError);
  if (filesystemError || !std::filesystem::is_directory(directory)) {
    error =
        "output directory is not writable: "
        + (filesystemError ? filesystemError.message() : directory.string());
    return false;
  }
  return true;
}

bool ReadScenarioBytes(const std::filesystem::path &path, std::string &bytes,
    std::string &error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open scenario file: " + path.string();
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad()) {
    error = "Could not read scenario file: " + path.string();
    return false;
  }
  bytes = buffer.str();
  return true;
}

bool WriteScenarioSnapshot(const std::filesystem::path &directory,
    const std::filesystem::path &sourcePath, std::string_view bytes,
    std::string &error) {
  const std::filesystem::path destination = directory / "scenario.yaml";
  std::error_code pathError;
  const std::filesystem::path absoluteSource =
      std::filesystem::absolute(sourcePath, pathError).lexically_normal();
  pathError.clear();
  const std::filesystem::path absoluteDestination =
      std::filesystem::absolute(destination, pathError).lexically_normal();
  if (!pathError && absoluteSource == absoluteDestination) {
    return true;
  }
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open scenario.yaml for writing";
    return false;
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    error = "could not write scenario.yaml";
    return false;
  }
  return true;
}

bool WriteManifest(const std::filesystem::path &directory,
    const SimulationRunInfo &info, const RunnerResult &result,
    std::string &error) {
  std::ofstream output(directory / "run.json",
      std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open run.json for writing";
    return false;
  }
  output << std::setprecision(17)
         << "{\n"
            "  \"contract_version\": \""
         << JSB_CONTRACT_VERSION
         << "\",\n"
            "  \"runtime\": {\n"
            "    \"branch\": \""
         << JsonEscape(JSB_RUNTIME_BRANCH)
         << "\",\n"
            "    \"commit\": \""
         << JsonEscape(JSB_GIT_COMMIT)
         << "\",\n"
            "    \"application_version\": \""
         << JsonEscape(JSB_APPLICATION_VERSION)
         << "\"\n"
            "  },\n"
            "  \"scenario\": {\n"
            "    \"name\": \""
         << JsonEscape(info.scenarioName)
         << "\",\n"
            "    \"file\": \""
         << JsonEscape(info.scenarioFile)
         << "\",\n"
            "    \"digest_sha256\": \""
         << JsonEscape(info.scenarioDigest)
         << "\",\n"
            "    \"schema_version\": "
         << info.scenarioSchemaVersion
         << ",\n"
            "    \"scenario_type\": \""
         << JsonEscape(info.scenarioType)
         << "\"\n"
            "  },\n"
            "  \"telemetry_schema_version\": "
         << JSB_TELEMETRY_SCHEMA_VERSION
         << ",\n"
            "  \"aircraft\": \""
         << JsonEscape(info.aircraft)
         << "\",\n"
            "  \"mode\": \""
         << ToString(info.mode)
         << "\",\n"
            "  \"execution\": {\n";
  if (info.mode == ExecutionMode::Compare) {
    output << "    \"variants\": [\"baseline\", \"primary\"]\n";
  } else {
    output << "    \"variant\": \""
           << (info.variant ? sim::ToString(*info.variant) : std::string_view{})
           << "\"\n";
  }
  output << "  },\n";
  if (info.mode == ExecutionMode::Single && info.variant) {
    output << "  \"autopilot\": \"" << sim::ToString(*info.variant) << "\",\n";
  }
  output << "  \"started_at\": \"" << JsonEscape(info.startedAt)
         << "\",\n"
            "  \"ended_at\": \""
         << JsonEscape(result.endedAt)
         << "\",\n"
            "  \"duration_s\": "
         << info.durationSec
         << ",\n"
            "  \"status\": \""
         << JsonEscape(result.status) << "\"";
  if (info.mode == ExecutionMode::Compare) {
    const std::string_view baselineStatus =
        result.baselineStatus.empty() ? std::string_view(result.status)
                                      : std::string_view(result.baselineStatus);
    const std::string_view primaryStatus =
        result.primaryStatus.empty() ? std::string_view(result.status)
                                     : std::string_view(result.primaryStatus);
    output << ",\n"
              "  \"results\": {\n"
              "    \"baseline\": {\"status\": \""
           << JsonEscape(baselineStatus) << '"';
    if (!result.baselineError.empty()) {
      output << ", \"error\": \"" << JsonEscape(result.baselineError) << '"';
    }
    output << "},\n"
              "    \"primary\": {\"status\": \""
           << JsonEscape(primaryStatus) << '"';
    if (!result.primaryError.empty()) {
      output << ", \"error\": \"" << JsonEscape(result.primaryError) << '"';
    }
    output << "}\n"
              "  }";
  }
  output << ",\n"
            "  \"simulation_dt_s\": "
         << info.dtSec
         << ",\n"
            "  \"simulation_time_s\": "
         << result.simulationTimeSec
         << ",\n"
            "  \"wall_time_s\": "
         << result.wallTimeSec
         << ",\n"
            "  \"realtime_factor\": "
         << result.realtimeFactor
         << ",\n"
            "  \"steps\": "
         << result.steps;
  if (!result.error.empty()) {
    output << ",\n"
              "  \"error\": \""
           << JsonEscape(result.error) << '"';
  }
  output << "\n}\n";
  if (!output) {
    error = "could not write run.json";
    return false;
  }
  return true;
}

void SetOutputFailure(RunnerResult &result, std::string error) {
  result.status = "failed";
  result.exitCode = RunnerExitCode::OutputFailure;
  if (result.error.empty()) {
    result.error = std::move(error);
  } else if (!error.empty() && result.error != error) {
    result.error += "; ";
    result.error += error;
  }
}

void WriteFinalManifest(const SimulationRunInfo &info, RunnerResult &result) {
  if (result.endedAt.empty()) {
    result.endedAt = GetWallClockTimestamp();
  }
  std::string error;
  if (!WriteManifest(info.outputDirectory, info, result, error)) {
    SetOutputFailure(result, std::move(error));
  }
}
} // namespace

void SimulationRunner::AddObserver(ISimulationRunObserver &observer) {
  observers_.push_back(&observer);
}

namespace {
class IExecutionSession {
public:
  virtual ~IExecutionSession() = default;
  virtual bool IsRunning() const = 0;
  virtual bool Tick() = 0;
  virtual void Stop() = 0;
  virtual void Shutdown() = 0;
  virtual double GetSimulationTimeSec() const = 0;
  virtual std::uint64_t GetStepCount() const = 0;
  virtual const std::string &GetLastError() const = 0;
  virtual SimulationRunObservation TakeObservation() = 0;
  virtual void PopulateVariantResults(RunnerResult &result) const = 0;
};

class SingleExecutionSession final : public IExecutionSession {
public:
  static std::unique_ptr<SingleExecutionSession> Create(
      const sim::ResolvedExecutionSpec &execution, std::string &error) {
    auto runtime = sim::SimulationRuntime::CreateForExecution(execution, error);
    if (runtime == nullptr) {
      return nullptr;
    }
    if (!runtime->RunExecution(execution)) {
      error = runtime->GetStatus().lastError;
      runtime->Shutdown();
      return nullptr;
    }
    return std::unique_ptr<SingleExecutionSession>(
        new SingleExecutionSession(std::move(runtime),
            execution.scenario.dtSec,
            execution.scenario.durationSec));
  }

  bool IsRunning() const override {
    return runtime_->GetScenarioStatus().has_value();
  }

  bool Tick() override {
    observation_ = {};
    if (!runtime_->Tick()) {
      lastError_ = runtime_->GetStatus().lastError;
      return false;
    }
    ++stepCount_;
    observation_.telemetry.simulationTimeSec = GetSimulationTimeSec();
    observation_.telemetry.primary = runtime_->CaptureRecordingSource();
    observation_.scenarioEvents = runtime_->TakeScenarioEvents();
    return true;
  }

  void Stop() override { runtime_->Stop(); }
  void Shutdown() override { runtime_->Shutdown(); }

  double GetSimulationTimeSec() const override {
    return std::min(durationSec_, static_cast<double>(stepCount_) * dtSec_);
  }

  std::uint64_t GetStepCount() const override { return stepCount_; }
  const std::string &GetLastError() const override { return lastError_; }

  SimulationRunObservation TakeObservation() override {
    return std::exchange(observation_, {});
  }

  void PopulateVariantResults(RunnerResult &) const override {}

private:
  SingleExecutionSession(std::unique_ptr<sim::SimulationRuntime> runtime,
      double dtSec, double durationSec)
      : runtime_(std::move(runtime)), dtSec_(dtSec), durationSec_(durationSec) {
    observation_.scenarioEvents = runtime_->TakeScenarioEvents();
  }

  std::unique_ptr<sim::SimulationRuntime> runtime_;
  double dtSec_ = 0.0;
  double durationSec_ = 0.0;
  std::uint64_t stepCount_ = 0;
  SimulationRunObservation observation_;
  std::string lastError_;
};

std::string ComparisonStatus(sim::ComparisonExecutionState state,
    std::string_view runStatus) {
  switch (state) {
  case sim::ComparisonExecutionState::Completed:
    return "completed";
  case sim::ComparisonExecutionState::Failed:
    return "failed";
  case sim::ComparisonExecutionState::Stopped:
    return runStatus == "interrupted" ? "interrupted" : "failed";
  case sim::ComparisonExecutionState::Running:
    return "failed";
  }
  return "failed";
}

class ComparisonExecutionSession final : public IExecutionSession {
public:
  static std::unique_ptr<ComparisonExecutionSession> Create(
      const sim::SimulationScenario &scenario,
      const sim::ScenarioSource &source, std::string &error) {
    auto comparison =
        sim::SimulationComparison::Create(scenario, source, error);
    return comparison == nullptr
               ? nullptr
               : std::unique_ptr<ComparisonExecutionSession>(
                     new ComparisonExecutionSession(std::move(comparison)));
  }

  bool IsRunning() const override { return comparison_->IsRunning(); }

  bool Tick() override {
    if (!comparison_->Tick()) {
      return false;
    }
    const sim::ComparisonObservation source = comparison_->TakeObservation();
    observation_.telemetry = source.telemetry;
    observation_.scenarioEvents = source.scenarioEvents;
    return true;
  }

  void Stop() override { comparison_->Stop(); }
  void Shutdown() override { comparison_->Shutdown(); }
  double GetSimulationTimeSec() const override {
    return comparison_->GetSimulationTimeSec();
  }
  std::uint64_t GetStepCount() const override {
    return comparison_->GetStepCount();
  }
  const std::string &GetLastError() const override {
    return comparison_->GetLastError();
  }

  SimulationRunObservation TakeObservation() override {
    return std::exchange(observation_, {});
  }

  void PopulateVariantResults(RunnerResult &result) const override {
    const sim::ComparisonVariantResult &baseline =
        comparison_->GetVariantResult(sim::ExecutionVariant::Baseline);
    const sim::ComparisonVariantResult &primary =
        comparison_->GetVariantResult(sim::ExecutionVariant::Primary);
    result.baselineStatus = ComparisonStatus(baseline.state, result.status);
    result.baselineError = baseline.error;
    result.primaryStatus = ComparisonStatus(primary.state, result.status);
    result.primaryError = primary.error;
    if (result.status != "completed") {
      if (result.baselineError.empty() && result.baselineStatus == "failed") {
        result.baselineError = result.error;
      }
      if (result.primaryError.empty() && result.primaryStatus == "failed") {
        result.primaryError = result.error;
      }
    }
  }

private:
  explicit ComparisonExecutionSession(
      std::unique_ptr<sim::SimulationComparison> comparison)
      : comparison_(std::move(comparison)) {
    const sim::ComparisonObservation source = comparison_->TakeObservation();
    observation_.telemetry = source.telemetry;
    observation_.scenarioEvents = source.scenarioEvents;
  }

  std::unique_ptr<sim::SimulationComparison> comparison_;
  SimulationRunObservation observation_;
};
} // namespace

RunnerResult SimulationRunner::Run(const RunnerOptions &options,
    const volatile std::sig_atomic_t *running) {
  RunnerResult result;
  std::string error;
  if (!PrepareOutputDirectory(options.outputDirectory, error)) {
    result.exitCode = RunnerExitCode::OutputFailure;
    result.error = std::move(error);
    return result;
  }

  std::error_code pathError;
  const std::filesystem::path absoluteScenarioPath =
      std::filesystem::absolute(options.scenarioPath, pathError);
  SimulationRunInfo info{
      .scenarioName = options.scenarioPath.stem().string(),
      .scenarioFile =
          (pathError ? options.scenarioPath : absoluteScenarioPath).string(),
      .startedAt = GetWallClockTimestamp(),
      .outputDirectory = options.outputDirectory,
      .mode = options.mode,
      .variant = options.variant,
  };

  std::string scenarioBytes;
  if (!ReadScenarioBytes(options.scenarioPath, scenarioBytes, error)) {
    result.exitCode = RunnerExitCode::ScenarioLoadFailure;
    result.error = std::move(error);
    WriteFinalManifest(info, result);
    return result;
  }
  info.scenarioDigest = common::crypto::Sha256Hex(scenarioBytes);
  if (!WriteScenarioSnapshot(options.outputDirectory,
          options.scenarioPath,
          scenarioBytes,
          error)) {
    result.exitCode = RunnerExitCode::OutputFailure;
    result.error = std::move(error);
    WriteFinalManifest(info, result);
    return result;
  }

  sim::SimulationScenario scenario;
  sim::ScenarioLoadMetadata loadMetadata;
  if (!sim::SimulationScenarioSerializer::Deserialize(scenarioBytes,
          scenario,
          error,
          &loadMetadata)) {
    result.exitCode = RunnerExitCode::ScenarioLoadFailure;
    result.error = std::move(error);
    WriteFinalManifest(info, result);
    return result;
  }
  for (const std::string &warning : loadMetadata.warnings) {
    std::cerr << "[runner] warning: " << warning << '\n';
  }
  if (loadMetadata.legacyVariant && options.mode == ExecutionMode::Compare) {
    result.exitCode = RunnerExitCode::ScenarioLoadFailure;
    result.error = "Deprecated scenario field 'autopilot' cannot be used in "
                   "compare mode. Remove it from the Scenario.";
    WriteFinalManifest(info, result);
    return result;
  }
  if (loadMetadata.legacyVariant && options.variant
      && *loadMetadata.legacyVariant != *options.variant) {
    result.exitCode = RunnerExitCode::ScenarioLoadFailure;
    result.error = "Scenario autopilot '"
                   + std::string(ToString(*loadMetadata.legacyVariant))
                   + "' conflicts with execution variant '"
                   + std::string(ToString(*options.variant)) + "'.";
    WriteFinalManifest(info, result);
    return result;
  }
  if (!sim::ValidateSimulationScenario(scenario, &error)) {
    result.exitCode = RunnerExitCode::ScenarioLoadFailure;
    result.error = std::move(error);
    info.scenarioName = scenario.name;
    info.durationSec = scenario.durationSec;
    WriteFinalManifest(info, result);
    return result;
  }

  info.scenarioName = scenario.name;
  info.scenarioSchemaVersion =
      static_cast<std::uint32_t>(scenario.schemaVersion);
  info.scenarioType = scenario.scenarioType;
  info.aircraft = scenario.aircraft;
  info.dtSec = scenario.dtSec;
  info.durationSec = scenario.durationSec;
  const sim::ScenarioSource source{
      .file = info.scenarioFile,
      .digestSha256 = info.scenarioDigest,
  };
  std::unique_ptr<IExecutionSession> session;
  if (options.mode == ExecutionMode::Compare) {
    session = ComparisonExecutionSession::Create(scenario, source, error);
  } else {
    sim::ResolvedExecutionSpec execution;
    if (!options.variant
        || !sim::ExecutionVariantResolver::Resolve(
            {
                .scenario = scenario,
                .variant = *options.variant,
                .source = source,
            },
            execution,
            error)) {
      result.exitCode = RunnerExitCode::ScenarioLoadFailure;
      result.error = error.empty() ? "single execution variant is missing"
                                   : std::move(error);
      WriteFinalManifest(info, result);
      return result;
    }
    session = SingleExecutionSession::Create(execution, error);
  }
  if (session == nullptr) {
    result.exitCode = RunnerExitCode::SimulationInitializationFailure;
    result.error = std::move(error);
    if (options.mode == ExecutionMode::Compare) {
      result.baselineStatus = "failed";
      result.primaryStatus = "failed";
      result.baselineError = result.error;
      result.primaryError = result.error;
    }
    WriteFinalManifest(info, result);
    return result;
  }

  std::vector<ISimulationRunObserver *> startedObservers;
  const SimulationRunObservation initialObservation =
      session->TakeObservation();
  for (ISimulationRunObserver *observer : observers_) {
    std::string observerError;
    if (!observer->OnRunStarted(info, initialObservation, observerError)) {
      result.exitCode = RunnerExitCode::OutputFailure;
      result.error = observerError.empty() ? "run observer failed to start"
                                           : std::move(observerError);
      session->Stop();
      result.simulationTimeSec = 0.0;
      session->PopulateVariantResults(result);
      for (ISimulationRunObserver *startedObserver : startedObservers) {
        observerError.clear();
        if (!startedObserver->OnRunFinished(info, result, observerError)) {
          SetOutputFailure(result,
              observerError.empty() ? "run observer failed to finalize"
                                    : std::move(observerError));
        }
      }
      session->Shutdown();
      WriteFinalManifest(info, result);
      return result;
    }
    startedObservers.push_back(observer);
  }

  std::cout << "[runner] scenario: " << scenario.name << '\n'
            << "[runner] mode: " << ToString(info.mode) << '\n';
  if (info.variant) {
    std::cout << "[runner] variant: " << sim::ToString(*info.variant) << '\n';
  } else {
    std::cout << "[runner] variants: baseline, primary\n";
  }
  std::cout << "[runner] dt: " << std::fixed << std::setprecision(6)
            << scenario.dtSec << '\n'
            << "[runner] duration: " << std::setprecision(3)
            << scenario.durationSec << '\n'
            << "[runner] starting\n";
  const Clock::time_point start = Clock::now();
  while (session->IsRunning()) {
    if (running != nullptr && *running == 0) {
      result.status = "interrupted";
      result.exitCode = RunnerExitCode::GeneralFailure;
      result.error = "interrupted by signal";
      session->Stop();
      break;
    }
    if (!session->Tick()) {
      result.status = "failed";
      result.exitCode = RunnerExitCode::SimulationExecutionFailure;
      result.error = session->GetLastError();
      session->Stop();
      break;
    }
    result.steps = session->GetStepCount();
    const SimulationRunObservation observation = session->TakeObservation();
    for (ISimulationRunObserver *observer : startedObservers) {
      std::string observerError;
      if (!observer->OnSimulationStep(info, observation, observerError)) {
        result.status = "failed";
        result.exitCode = RunnerExitCode::OutputFailure;
        result.error = observerError.empty()
                           ? "run observer failed to consume simulation step"
                           : std::move(observerError);
        session->Stop();
        break;
      }
    }
    if (result.exitCode == RunnerExitCode::OutputFailure) {
      break;
    }
  }
  const std::chrono::duration<double> wallDuration = Clock::now() - start;
  result.wallTimeSec = wallDuration.count();
  result.steps = session->GetStepCount();
  result.simulationTimeSec = session->GetSimulationTimeSec();
  result.realtimeFactor = result.wallTimeSec > 0.0
                              ? result.simulationTimeSec / result.wallTimeSec
                              : 0.0;
  if (result.error.empty()) {
    result.status = "completed";
    result.exitCode = RunnerExitCode::Success;
  }
  session->PopulateVariantResults(result);
  for (ISimulationRunObserver *observer : startedObservers) {
    std::string observerError;
    if (!observer->OnRunFinished(info, result, observerError)) {
      SetOutputFailure(result,
          observerError.empty() ? "run observer failed to finalize"
                                : std::move(observerError));
    }
  }
  session->Shutdown();

  WriteFinalManifest(info, result);
  std::cout << "[runner] " << result.status << '\n'
            << "[runner] sim time: " << std::fixed << std::setprecision(2)
            << result.simulationTimeSec << " s\n"
            << "[runner] wall time: " << std::setprecision(2)
            << result.wallTimeSec << " s\n"
            << "[runner] speed: " << std::setprecision(2)
            << result.realtimeFactor << "x realtime\n";
  return result;
}
} // namespace runner
