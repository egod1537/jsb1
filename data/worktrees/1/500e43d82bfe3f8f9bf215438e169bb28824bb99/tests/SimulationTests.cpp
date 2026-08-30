#include "sim/Aircraft.hpp"
#include "sim/ErrorTracker.hpp"
#include "sim/Simulation.hpp"
#include "sim/StateLogger.hpp"
#include "sim/gnc/ControlContext.hpp"
#include "sim/gnc/autopilot/IAutopilot.hpp"
#include "sim/gnc/autopilot/IAutopilotAnalysis.hpp"
#include "sim/gnc/autopilot/IControllerInspectable.hpp"
#include "sim/gnc/autopilot/ITrimReferenceConsumer.hpp"
#include "sim/gnc/autopilot/MyAutopilot.hpp"
#include "sim/gnc/autopilot/PX4Autopilot.hpp"
#include "sim/gnc/hold/AirspeedHoldController.hpp"
#include "sim/gnc/hold/AltitudeHoldController.hpp"
#include "sim/gnc/hold/CourseHoldController.hpp"
#include "sim/gnc/hold/PitchHoldController.hpp"
#include "sim/gnc/hold/YawDamperController.hpp"
#include "sim/control/FlightControlManager.hpp"
#include "sim/control/FlightControlMode.hpp"
#include "sim/telemetry/AircraftTelemetry.hpp"
#include "sim/telemetry/AutopilotTelemetry.hpp"
#include "common/math/Math.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
static_assert(std::is_base_of_v<gnc::IAutopilot, gnc::MyAutopilot>);
static_assert(std::is_base_of_v<gnc::IAutopilot, gnc::PX4Autopilot>);
static_assert(std::is_base_of_v<gnc::IAutopilotAnalysis, gnc::MyAutopilot>);
static_assert(std::is_base_of_v<gnc::IControllerInspectable, gnc::MyAutopilot>);
static_assert(std::is_base_of_v<gnc::ITrimReferenceConsumer, gnc::MyAutopilot>);
static_assert(!std::is_base_of_v<gnc::IAutopilotAnalysis, gnc::PX4Autopilot>);
static_assert(
    std::is_base_of_v<gnc::IControllerInspectable, gnc::PX4Autopilot>);
static_assert(
    std::is_base_of_v<gnc::ITrimReferenceConsumer, gnc::PX4Autopilot>);

template <typename T>
concept HasMyAutopilotTuningApi = requires(T &autopilot,
    const gnc::RollHoldSettings &settings) {
  autopilot.SetRollHoldSettings(settings);
};

template <typename T>
concept HasLegacyRollComparisonApi = requires(T &autopilot) {
  autopilot.SetPx4RollComparisonEnabled(true);
  autopilot.GetRollHoldControlSource();
};

template <typename T>
concept HasTrimOwnershipApi = requires(T &autopilot, sim::Aircraft &aircraft,
    const gnc::TrimRequest &request) {
  autopilot.ComputeTrim(aircraft, request);
  autopilot.GetTrimResult();
};

template <typename T>
concept HasLinearizationApi = requires(T &autopilot) {
  autopilot.GetLinearizationResult();
  autopilot.GetDynamicModeHistory();
};

template <typename T>
concept HasControllerInspectionApi = requires(T &autopilot) {
  autopilot.template GetController<gnc::RollHoldController>();
};

static_assert(HasMyAutopilotTuningApi<gnc::MyAutopilot>);
static_assert(!HasMyAutopilotTuningApi<gnc::IAutopilot>);
static_assert(!HasMyAutopilotTuningApi<gnc::PX4Autopilot>);
static_assert(!HasLegacyRollComparisonApi<gnc::MyAutopilot>);
static_assert(!HasTrimOwnershipApi<gnc::IAutopilot>);
static_assert(!HasTrimOwnershipApi<gnc::PX4Autopilot>);
static_assert(!HasLinearizationApi<gnc::IAutopilot>);
static_assert(!HasLinearizationApi<gnc::PX4Autopilot>);
static_assert(!HasControllerInspectionApi<gnc::IAutopilot>);
static_assert(HasControllerInspectionApi<gnc::PX4Autopilot>);

constexpr double SimTimeTolerance = 1.0e-9;
constexpr double MultiInstanceStateTolerance = 1.0e-5;
constexpr double AltitudeToleranceFt = 1.0;
constexpr double AirspeedToleranceKts = 0.5;
constexpr double HeadingToleranceDeg = 0.5;
constexpr double TrimInputTolerance = 1.0e-5;
constexpr double ControlCommandTolerance = 1.0e-6;
constexpr double MaximumAsyncKickoffSec = 1.0;
constexpr double ExpectedLinearizationRefreshIntervalSec = 5.0;

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireNear(double actual, double expected, double tolerance,
    const std::string &message) {
  if (std::fabs(actual - expected) > tolerance) {
    throw std::runtime_error(message + " actual=" + std::to_string(actual)
                             + " expected=" + std::to_string(expected));
  }
}

template <std::size_t Size>
void RequireArrayNear(const std::array<double, Size> &actual,
    const std::array<double, Size> &expected, double tolerance,
    const std::string &message) {
  for (std::size_t index = 0; index < Size; ++index) {
    RequireNear(actual[index],
        expected[index],
        tolerance,
        message + "[" + std::to_string(index) + "]");
  }
}

void RequireVectorNear(const std::vector<double> &actual,
    const std::vector<double> &expected, double tolerance,
    const std::string &message) {
  Require(actual.size() == expected.size(), message + " size mismatch");
  for (std::size_t index = 0; index < actual.size(); ++index) {
    RequireNear(actual[index],
        expected[index],
        tolerance,
        message + "[" + std::to_string(index) + "]");
  }
}

void RequireKinematicStateNear(const sim::FDMKinematicState &actual,
    const sim::FDMKinematicState &expected, const std::string &message,
    double tolerance = SimTimeTolerance) {
  RequireNear(actual.latitudeRad,
      expected.latitudeRad,
      tolerance,
      message + " latitude mismatch");
  RequireNear(actual.longitudeRad,
      expected.longitudeRad,
      tolerance,
      message + " longitude mismatch");
  RequireNear(actual.altitudeAslFt,
      expected.altitudeAslFt,
      tolerance,
      message + " altitude mismatch");
  RequireArrayNear(actual.bodyVelocityFps,
      expected.bodyVelocityFps,
      tolerance,
      message + " body velocity mismatch");
  RequireArrayNear(actual.attitudeRad,
      expected.attitudeRad,
      tolerance,
      message + " attitude mismatch");
  RequireArrayNear(actual.bodyAngularRatesRadPerSec,
      expected.bodyAngularRatesRadPerSec,
      tolerance,
      message + " angular rate mismatch");
}

void RequireControlStateNear(const sim::FDMControlState &actual,
    const sim::FDMControlState &expected, const std::string &message,
    double tolerance = SimTimeTolerance) {
  RequireNear(actual.elevatorCommand,
      expected.elevatorCommand,
      tolerance,
      message + " elevator command mismatch");
  RequireNear(actual.aileronCommand,
      expected.aileronCommand,
      tolerance,
      message + " aileron command mismatch");
  RequireNear(actual.rudderCommand,
      expected.rudderCommand,
      tolerance,
      message + " rudder command mismatch");
  RequireVectorNear(actual.throttleCommands,
      expected.throttleCommands,
      tolerance,
      message + " throttle command mismatch");
  RequireNear(actual.pitchTrimCommand,
      expected.pitchTrimCommand,
      tolerance,
      message + " pitch trim mismatch");
  RequireNear(actual.elevatorPositionRad,
      expected.elevatorPositionRad,
      tolerance,
      message + " elevator position mismatch");
  RequireNear(actual.leftAileronPositionRad,
      expected.leftAileronPositionRad,
      tolerance,
      message + " left aileron position mismatch");
  RequireNear(actual.rightAileronPositionRad,
      expected.rightAileronPositionRad,
      tolerance,
      message + " right aileron position mismatch");
  RequireNear(actual.rudderPositionRad,
      expected.rudderPositionRad,
      tolerance,
      message + " rudder position mismatch");
  RequireVectorNear(actual.throttlePositions,
      expected.throttlePositions,
      tolerance,
      message + " throttle position mismatch");
}

void RequirePropulsionStateNear(const sim::FDMPropulsionState &actual,
    const sim::FDMPropulsionState &expected, const std::string &message,
    double tolerance = SimTimeTolerance) {
  Require(actual.engines.size() == expected.engines.size(),
      message + " engine count mismatch");
  for (std::size_t index = 0; index < actual.engines.size(); ++index) {
    Require(actual.engines[index].running == expected.engines[index].running,
        message + " engine running state mismatch");
    RequireNear(actual.engines[index].engineRpm,
        expected.engines[index].engineRpm,
        tolerance,
        message + " engine RPM mismatch");
    RequireNear(actual.engines[index].thrusterRpm,
        expected.engines[index].thrusterRpm,
        tolerance,
        message + " thruster RPM mismatch");
  }
}

void RequireEnvironmentStateNear(const sim::FDMEnvironmentState &actual,
    const sim::FDMEnvironmentState &expected, const std::string &message,
    double tolerance = SimTimeTolerance) {
  RequireNear(actual.seaLevelTemperatureRankine,
      expected.seaLevelTemperatureRankine,
      tolerance,
      message + " sea-level temperature mismatch");
  RequireNear(actual.seaLevelPressurePsf,
      expected.seaLevelPressurePsf,
      tolerance,
      message + " sea-level pressure mismatch");
  Require(actual.hasStandardAtmosphere == expected.hasStandardAtmosphere,
      message + " atmosphere model mismatch");
  RequireNear(actual.temperatureBiasRankine,
      expected.temperatureBiasRankine,
      tolerance,
      message + " temperature bias mismatch");
  RequireNear(actual.seaLevelGradedTemperatureDeltaRankine,
      expected.seaLevelGradedTemperatureDeltaRankine,
      tolerance,
      message + " graded temperature mismatch");
  RequireNear(actual.vaporMassFractionPpm,
      expected.vaporMassFractionPpm,
      tolerance,
      message + " vapor fraction mismatch");
  RequireArrayNear(actual.windNedFps,
      expected.windNedFps,
      tolerance,
      message + " wind mismatch");
  RequireArrayNear(actual.gustNedFps,
      expected.gustNedFps,
      tolerance,
      message + " gust mismatch");
  RequireArrayNear(actual.turbulenceNedFps,
      expected.turbulenceNedFps,
      tolerance,
      message + " turbulence mismatch");
  Require(actual.turbulenceType == expected.turbulenceType,
      message + " turbulence type mismatch");
  RequireNear(actual.turbulenceGain,
      expected.turbulenceGain,
      tolerance,
      message + " turbulence gain mismatch");
  RequireNear(actual.turbulenceRate,
      expected.turbulenceRate,
      tolerance,
      message + " turbulence rate mismatch");
  RequireNear(actual.turbulenceRhythmicity,
      expected.turbulenceRhythmicity,
      tolerance,
      message + " turbulence rhythmicity mismatch");
  RequireNear(actual.windSpeedAt20FtFps,
      expected.windSpeedAt20FtFps,
      tolerance,
      message + " 20-foot wind mismatch");
  RequireNear(actual.terrainElevationFt,
      expected.terrainElevationFt,
      tolerance,
      message + " terrain elevation mismatch");
  Require(actual.gravityType == expected.gravityType,
      message + " gravity type mismatch");
  RequireNear(actual.planetRotationRateRadPerSec,
      expected.planetRotationRateRadPerSec,
      tolerance,
      message + " planet rotation mismatch");
}

void RequireFDMStateNear(const sim::FDMState &actual,
    const sim::FDMState &expected, const std::string &message,
    double tolerance = SimTimeTolerance) {
  Require(actual.flags == expected.flags, message + " flags mismatch");
  RequireKinematicStateNear(actual.state,
      expected.state,
      message + " state",
      tolerance);
  RequireControlStateNear(actual.controls,
      expected.controls,
      message + " controls",
      tolerance);
  RequirePropulsionStateNear(actual.propulsion,
      expected.propulsion,
      message + " propulsion",
      tolerance);
  RequireEnvironmentStateNear(actual.environment,
      expected.environment,
      message + " environment",
      tolerance);
}

void RequireTelemetryNear(const telemetry::TelemetryRegistry &actual,
    const telemetry::TelemetryRegistry &expected, const std::string &message,
    double tolerance = SimTimeTolerance) {
  const auto actualPaths = actual.GetChannelPaths();
  const auto expectedPaths = expected.GetChannelPaths();
  Require(actualPaths == expectedPaths, message + " channel paths mismatch");

  for (const std::string_view path : actualPaths) {
    const auto *actualChannel = actual.Find(path);
    const auto *expectedChannel = expected.Find(path);
    Require(actualChannel != nullptr && expectedChannel != nullptr,
        message + " channel lookup mismatch");

    const auto &actualSamples = actualChannel->GetSamples();
    const auto &expectedSamples = expectedChannel->GetSamples();
    Require(actualSamples.GetSize() == expectedSamples.GetSize(),
        message + " sample count mismatch");
    for (std::size_t index = 0; index < actualSamples.GetSize(); ++index) {
      RequireNear(actualSamples[index].timeSec,
          expectedSamples[index].timeSec,
          tolerance,
          message + " sample time mismatch");
      RequireNear(actualSamples[index].value,
          expectedSamples[index].value,
          tolerance,
          message + " sample value mismatch");
    }
  }
}

sim::SimulationConfig MakeConfig() {
  sim::SimulationConfig config{};
  config.simulationHz = 120.0;
  return config;
}

void StartSimulation(sim::Simulation &simulation) {
  Require(simulation.Initialize(MakeConfig()),
      "Simulation failed to initialize");
}

double GetSimTime(const sim::Simulation &simulation) {
  return simulation.GetAircraft().GetAircraftState().simulationTimeSec;
}

sim::Tick MakeTestTick(const sim::Simulation &simulation) {
  return {0U, simulation.GetTickSizeSec(), GetSimTime(simulation)};
}

control::FlightControlManager &GetFlightControlManager(
    sim::Simulation &simulation) {
  auto *flightControlManager =
      simulation.GetComponent<control::FlightControlManager>();
  Require(flightControlManager != nullptr,
      "Simulation does not contain FlightControlManager");
  return *flightControlManager;
}

gnc::MyAutopilot &GetMyAutopilot(sim::Simulation &simulation) {
  auto *autopilot = dynamic_cast<gnc::MyAutopilot *>(
      &GetFlightControlManager(simulation).GetAutopilot());
  Require(autopilot != nullptr, "Simulation does not use MyAutopilot");
  return *autopilot;
}

void WaitForAutopilotDynamics(sim::Simulation &simulation,
    gnc::MyAutopilot &autopilot) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (!autopilot.GetRollDynamics()) {
    Require(std::chrono::steady_clock::now() < deadline,
        "Timed out waiting for asynchronous Roll Hold dynamics");
    Require(simulation.Tick(),
        "Simulation tick failed while waiting for Roll Hold dynamics");
    std::this_thread::yield();
  }
}

void WaitForLinearizationResult(sim::Simulation &simulation,
    gnc::MyAutopilot &autopilot) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (autopilot.GetLinearizationResult() == nullptr) {
    Require(std::chrono::steady_clock::now() < deadline,
        "Timed out waiting for asynchronous linearization");
    Require(simulation.Tick(),
        "Simulation tick failed while waiting for linearization");
    std::this_thread::yield();
  }
}

struct ComponentLifecycleCounts {
  int initialize = 0;
  int reset = 0;
  int preTick = 0;
  int tick = 0;
  int postTick = 0;
  int shutdown = 0;
};

class LifecycleTestComponent final : public sim::Component {
public:
  explicit LifecycleTestComponent(ComponentLifecycleCounts &counts)
      : counts_(counts) {}

protected:
  bool OnInitialize() override {
    ++counts_.initialize;
    return true;
  }
  bool OnReset() override {
    ++counts_.reset;
    return true;
  }
  bool OnPreTick(const sim::Tick &) override {
    ++counts_.preTick;
    return true;
  }
  bool OnTick(const sim::Tick &) override {
    ++counts_.tick;
    return true;
  }
  bool OnPostTick(const sim::Tick &) override {
    ++counts_.postTick;
    return true;
  }
  void OnShutdown() override { ++counts_.shutdown; }

private:
  ComponentLifecycleCounts &counts_;
};

class ComponentLookupTestComponent final : public sim::Component {
public:
  bool FoundLifecycleComponent() const { return foundLifecycleComponent_; }
  bool HasAircraftAccess() const { return hasAircraftAccess_; }

protected:
  bool OnInitialize() override {
    foundLifecycleComponent_ =
        GetComponent<LifecycleTestComponent>() != nullptr;
    hasAircraftAccess_ =
        std::isfinite(GetAircraft().GetAircraftState().simulationTimeSec);
    return foundLifecycleComponent_ && hasAircraftAccess_;
  }

private:
  bool foundLifecycleComponent_ = false;
  bool hasAircraftAccess_ = false;
};

class RegistryTestController final : public gnc::Controller {
public:
  explicit RegistryTestController(int &resetCount) : resetCount_(resetCount) {}

  void Reset() override { ++resetCount_; }

private:
  int &resetCount_;
};

void TestErrorTrackerOwnsErrorState() {
  sim::ErrorTracker errorTracker;
  Require(!errorTracker.HasError(), "New error tracker contains an error");

  errorTracker.SetError("specific error");
  errorTracker.SetErrorIfEmpty("fallback error");
  Require(errorTracker.GetLastError() == "specific error",
      "Fallback replaced a specific error");

  errorTracker.ClearError();
  Require(!errorTracker.HasError(), "Error tracker did not clear its error");

  errorTracker.SetErrorIfEmpty("fallback error");
  Require(errorTracker.GetLastError() == "fallback error",
      "Fallback error was not stored");
}

void TestSimulationComponentLifecycle() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  ComponentLifecycleCounts counts;
  auto *component = simulation.AddComponent<LifecycleTestComponent>(counts);
  auto *lookup = simulation.AddComponent<ComponentLookupTestComponent>();

  Require(component != nullptr, "Failed to add lifecycle test component");
  Require(lookup != nullptr, "Failed to add component lookup test component");
  Require(simulation.GetComponent<sim::StateLogger>() != nullptr,
      "Simulation does not contain StateLogger");
  Require(counts.initialize == 0,
      "Component initialized before Simulation initialization");
  Require(simulation.GetComponent<LifecycleTestComponent>() == component,
      "GetComponent did not return the added component");

  StartSimulation(simulation);
  Require(counts.initialize == 1, "Component was not initialized");
  Require(lookup->FoundLifecycleComponent(),
      "Component could not find another component through its owner");
  Require(lookup->HasAircraftAccess(),
      "Component could not access Aircraft through its protected helper");

  const sim::Simulation &constSimulation = simulation;
  Require(constSimulation.GetComponent<ComponentLookupTestComponent>()
              == lookup,
      "Const GetComponent did not return the added component");

  Require(simulation.Tick(), "Component lifecycle tick failed");
  Require(counts.preTick == 1 && counts.tick == 1 && counts.postTick == 1,
      "Component tick lifecycle hooks were not called");

  Require(simulation.Reset(), "Component lifecycle reset failed");
  Require(counts.reset == 1, "Component reset hook was not called");

  Require(simulation.RemoveComponent<LifecycleTestComponent>(),
      "Failed to remove lifecycle test component");
  Require(counts.shutdown == 1,
      "Removing a component did not run its shutdown hook");
  Require(simulation.GetComponent<LifecycleTestComponent>() == nullptr,
      "Removed component is still accessible");

  ComponentLifecycleCounts lateCounts;
  auto *lateComponent =
      simulation.AddComponent<LifecycleTestComponent>(lateCounts);
  Require(lateComponent != nullptr, "Failed to add late component");
  Require(lateCounts.initialize == 1,
      "Component added after initialization was not initialized immediately");
  Require(simulation.RemoveComponent<LifecycleTestComponent>(),
      "Failed to remove late component");
  Require(lateCounts.shutdown == 1,
      "Late component shutdown hook was not called");

  simulation.Shutdown();
}

void TestTickAdvancesOneStep() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);

  const double startTime = GetSimTime(simulation);
  Require(simulation.Tick(), "Simulation tick failed");
  RequireNear(GetSimTime(simulation),
      startTime + simulation.GetTickSizeSec(),
      SimTimeTolerance,
      "Tick did not advance exactly one simulation step");
}

void TestStepUsesRequestedDeltaTime() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);

  constexpr double RequestedDtSec = 1.0 / 240.0;
  Require(simulation.Step(RequestedDtSec),
      "Simulation step with an explicit delta time failed");
  RequireNear(simulation.GetTime(),
      RequestedDtSec,
      SimTimeTolerance,
      "Step did not use the requested delta time");

  const double timeBeforeInvalidStep = simulation.GetTime();
  Require(!simulation.Step(0.0), "Simulation accepted a zero delta time");
  RequireNear(simulation.GetTime(),
      timeBeforeInvalidStep,
      SimTimeTolerance,
      "Invalid Step changed simulation time");
}

void TestSimulationInstancesAreIsolatedAndDeterministic() {
  sim::Simulation custom(std::make_unique<gnc::MyAutopilot>());
  sim::Simulation baseline(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(custom);
  StartSimulation(baseline);

  auto &customManager = GetFlightControlManager(custom);
  auto &baselineManager = GetFlightControlManager(baseline);
  auto &customAutopilot = GetMyAutopilot(custom);
  auto &baselineAutopilot = GetMyAutopilot(baseline);
  customAutopilot.SetAutomaticLinearizationEnabled(false);
  baselineAutopilot.SetAutomaticLinearizationEnabled(false);

  Require(&custom.GetAircraft() != &baseline.GetAircraft(),
      "Simulation instances share an Aircraft");
  Require(&customAutopilot != &baselineAutopilot,
      "Simulation instances share an Autopilot");
  Require(&custom.GetTelemetry() != &baseline.GetTelemetry(),
      "Simulation instances share a TelemetryRegistry");
  Require(custom.GetTrimService().GetResult()
              != baseline.GetTrimService().GetResult(),
      "Simulation instances share a trim result object");
  Require(baseline.GetTrimService().GetResult() != nullptr,
      "Baseline Simulation is missing its trim result");
  const gnc::TrimResult baselineTrimBeforeCustomChanges =
      *baseline.GetTrimService().GetResult();

  const sim::FDMState baselineStateBeforeCustomStep =
      baseline.GetAircraft().ExtractFDMState(sim::FDMStateFlags::All);
  const control::ControlInput baselineCommandBeforeCustomStep =
      baselineManager.GetManualController().GetCommandedInput();

  gnc::RollHoldSettings rollSettings = customAutopilot.GetRollHoldSettings();
  rollSettings.targetRollRad =
      custom.GetAircraft().GetProperties().Roll().Rad() + 0.2;
  customAutopilot.SetRollHoldSettings(rollSettings);
  customAutopilot.SetRollHoldEnabled(true);
  customManager.SetMode(control::FlightControlMode::Autopilot);

  constexpr double ExplicitDtSec = 1.0 / 240.0;
  Require(custom.Step(ExplicitDtSec), "Custom Simulation step failed");
  RequireNear(custom.GetTime(),
      ExplicitDtSec,
      SimTimeTolerance,
      "Custom Simulation time mismatch");
  RequireNear(baseline.GetTime(),
      0.0,
      SimTimeTolerance,
      "Stepping custom advanced baseline time");
  RequireFDMStateNear(
      baseline.GetAircraft().ExtractFDMState(sim::FDMStateFlags::All),
      baselineStateBeforeCustomStep,
      "Stepping custom changed baseline FDM state");
  Require(baselineManager.GetManualController().GetCommandedInput()
              == baselineCommandBeforeCustomStep,
      "Stepping custom changed baseline command state");
  Require(baseline.GetTelemetry().GetChannelPaths().empty(),
      "Stepping custom published baseline telemetry");
  Require(!custom.GetTelemetry().GetChannelPaths().empty(),
      "Stepping custom did not publish custom telemetry");

  const auto *customRoll =
      customAutopilot.GetController<gnc::RollHoldController>();
  const auto *baselineRoll =
      baselineAutopilot.GetController<gnc::RollHoldController>();
  Require(customRoll != nullptr && baselineRoll != nullptr,
      "Simulation isolation test is missing Roll Hold controllers");
  Require(customRoll->IsEnabled(),
      "Primary Roll Hold state was not retained");
  Require(!baselineRoll->IsEnabled(),
      "Primary Roll Hold state leaked into Baseline");
  RequireNear(baselineRoll->GetSettings().targetRollRad,
      0.0,
      SimTimeTolerance,
      "Primary Roll Hold target leaked into Baseline");

  Require(custom.Reset(), "Custom Simulation reset failed");
  RequireNear(custom.GetTime(),
      0.0,
      SimTimeTolerance,
      "Custom reset did not reset custom time");
  RequireNear(baseline.GetTime(),
      0.0,
      SimTimeTolerance,
      "Custom reset changed baseline time");
  Require(custom.GetTelemetry().GetChannelPaths().empty(),
      "Custom reset did not clear custom telemetry");
  Require(baseline.GetTelemetry().GetChannelPaths().empty(),
      "Custom reset changed baseline telemetry");
  const gnc::TrimResult *baselineTrimAfterCustomReset =
      baseline.GetTrimService().GetResult();
  Require(baselineTrimAfterCustomReset != nullptr,
      "Custom reset removed baseline trim state");
  RequireNear(baselineTrimAfterCustomReset->elevator,
      baselineTrimBeforeCustomChanges.elevator,
      SimTimeTolerance,
      "Custom reset changed baseline trim elevator");
  RequireNear(baselineTrimAfterCustomReset->aileron,
      baselineTrimBeforeCustomChanges.aileron,
      SimTimeTolerance,
      "Custom reset changed baseline trim aileron");
  RequireNear(baselineTrimAfterCustomReset->rudder,
      baselineTrimBeforeCustomChanges.rudder,
      SimTimeTolerance,
      "Custom reset changed baseline trim rudder");
  RequireNear(baselineTrimAfterCustomReset->throttle,
      baselineTrimBeforeCustomChanges.throttle,
      SimTimeTolerance,
      "Custom reset changed baseline trim throttle");

  customAutopilot.SetRollHoldEnabled(false);
  customAutopilot.SetTargetRollRad(0.0);
  customManager.SetMode(control::FlightControlMode::Manual);
  baselineManager.SetMode(control::FlightControlMode::Manual);
  const control::ControlInput sharedCommand{
      .elevator = -0.02,
      .aileron = 0.03,
      .rudder = -0.01,
      .throttle = 0.55,
  };
  customManager.GetManualController().SetCommandedInput(sharedCommand);
  baselineManager.GetManualController().SetCommandedInput(sharedCommand);

  constexpr int StepCount = 24;
  for (int index = 0; index < StepCount; ++index) {
    Require(custom.Step(ExplicitDtSec),
        "Custom deterministic Simulation step failed");
    Require(baseline.Step(ExplicitDtSec),
        "Baseline deterministic Simulation step failed");
  }

  const double expectedTimeSec = StepCount * ExplicitDtSec;
  RequireNear(custom.GetTime(),
      expectedTimeSec,
      SimTimeTolerance,
      "Custom deterministic time mismatch");
  RequireNear(baseline.GetTime(),
      expectedTimeSec,
      SimTimeTolerance,
      "Baseline deterministic time mismatch");
  RequireFDMStateNear(
      custom.GetAircraft().ExtractFDMState(sim::FDMStateFlags::All),
      baseline.GetAircraft().ExtractFDMState(sim::FDMStateFlags::All),
      "Identically stepped Simulation state mismatch",
      MultiInstanceStateTolerance);
  RequireTelemetryNear(custom.GetTelemetry(),
      baseline.GetTelemetry(),
      "Identically stepped Simulation telemetry mismatch",
      MultiInstanceStateTolerance);

  const sim::FDMState baselineStateBeforeCustomReset =
      baseline.GetAircraft().ExtractFDMState(sim::FDMStateFlags::All);
  const auto *baselineRollChannel =
      baseline.GetTelemetry().Find(telemetry::paths::AircraftAttitudeRoll);
  Require(baselineRollChannel != nullptr,
      "Baseline roll telemetry is missing before isolation reset");
  const std::size_t baselineRollSampleCount =
      baselineRollChannel->GetSamples().GetSize();
  const telemetry::TelemetrySample baselineLatestRoll =
      *baselineRollChannel->GetLatest();

  Require(custom.Reset(), "Second custom Simulation reset failed");
  RequireNear(baseline.GetTime(),
      expectedTimeSec,
      SimTimeTolerance,
      "Resetting custom changed baseline time");
  RequireFDMStateNear(
      baseline.GetAircraft().ExtractFDMState(sim::FDMStateFlags::All),
      baselineStateBeforeCustomReset,
      "Resetting custom changed baseline FDM state");
  Require(baselineRollChannel->GetSamples().GetSize()
              == baselineRollSampleCount,
      "Resetting custom changed baseline telemetry history");
  RequireNear(baselineRollChannel->GetLatest()->timeSec,
      baselineLatestRoll.timeSec,
      SimTimeTolerance,
      "Resetting custom changed baseline telemetry time");
  RequireNear(baselineRollChannel->GetLatest()->value,
      baselineLatestRoll.value,
      SimTimeTolerance,
      "Resetting custom changed baseline telemetry value");
  Require(baselineManager.GetManualController().GetCommandedInput()
              == sharedCommand,
      "Resetting custom changed baseline command state");
}

void TestSimulationPublishesAircraftTelemetry() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);

  Require(simulation.GetTelemetryRegistry().GetChannelPaths().empty(),
      "Telemetry registry was not empty before the first tick");
  Require(simulation.Tick(), "First telemetry-producing tick failed");
  Require(simulation.Tick(), "Second telemetry-producing tick failed");

  constexpr std::string_view ExpectedPaths[] = {
      telemetry::paths::AircraftAeroAlpha,
      telemetry::paths::AircraftAeroBeta,
      telemetry::paths::AircraftAttitudeRoll,
      telemetry::paths::AircraftAttitudePitch,
      telemetry::paths::AircraftAttitudeHeading,
      telemetry::paths::AircraftNavigationCourse,
      telemetry::paths::AircraftBodyVelocityU,
      telemetry::paths::AircraftBodyVelocityV,
      telemetry::paths::AircraftBodyVelocityW,
      telemetry::paths::AircraftRateP,
      telemetry::paths::AircraftRateQ,
      telemetry::paths::AircraftRateR,
      telemetry::paths::AircraftCalibratedAirspeed,
      telemetry::paths::AircraftTrueAirspeed,
      telemetry::paths::AircraftAltitudeAgl,
      telemetry::paths::AircraftBodyAccelerationU,
      telemetry::paths::AircraftBodyAccelerationV,
      telemetry::paths::AircraftBodyAccelerationW,
      telemetry::paths::AircraftAngularAccelerationP,
      telemetry::paths::AircraftAngularAccelerationQ,
      telemetry::paths::AircraftAngularAccelerationR,
      telemetry::paths::AircraftControlAileron,
      telemetry::paths::AircraftControlRudder,
      telemetry::paths::AutopilotRollHoldCommandedRoll,
  };
  const auto &telemetry = simulation.GetTelemetryRegistry();
  Require(telemetry.GetChannelPaths().size() == std::size(ExpectedPaths),
      "Simulation published an unexpected number of telemetry channels");
  for (const std::string_view path : ExpectedPaths) {
    const telemetry::TelemetryChannel *channel = telemetry.Find(path);
    Require(channel != nullptr,
        "Simulation did not publish telemetry channel: " + std::string(path));
    Require(channel->GetSamples().GetSize() == 2,
        "Telemetry channel has the wrong sample count: " + std::string(path));
  }

  const sim::AircraftState state = simulation.GetAircraft().GetAircraftState();
  const auto *rollRateChannel = telemetry.Find(telemetry::paths::AircraftRateP);
  const telemetry::TelemetrySample *latest = rollRateChannel->GetLatest();
  Require(latest != nullptr,
      "Published roll-rate channel has no latest sample");
  RequireNear(latest->timeSec,
      state.simulationTimeSec,
      SimTimeTolerance,
      "Telemetry sample time did not match simulation time");
  RequireNear(latest->value,
      state.pDegPerSec,
      SimTimeTolerance,
      "Telemetry roll rate did not match aircraft state");

  const auto *courseChannel =
      telemetry.Find(telemetry::paths::AircraftNavigationCourse);
  RequireNear(courseChannel->GetLatest()->value,
      state.courseDeg,
      SimTimeTolerance,
      "Telemetry course did not match the aircraft ground course");
  const control::ControlInput &input =
      simulation.GetAircraft().GetControls().GetInput();
  RequireNear(telemetry.Find(telemetry::paths::AircraftControlAileron)
                  ->GetLatest()
                  ->value,
      input.aileron,
      SimTimeTolerance,
      "Telemetry aileron did not match the final applied input");
  RequireNear(telemetry.Find(telemetry::paths::AircraftControlRudder)
                  ->GetLatest()
                  ->value,
      input.rudder,
      SimTimeTolerance,
      "Telemetry rudder did not match the final applied input");

  Require(simulation.Reset(),
      "Telemetry reset test failed to reset simulation");
  Require(simulation.GetTelemetryRegistry().GetChannelPaths().empty(),
      "Simulation reset did not clear telemetry history");
}

void TestResetUsesDefaultInitialCondition() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  for (int i = 0; i < 3; ++i) {
    Require(simulation.Tick(), "Pre-reset tick failed");
  }

  Require(simulation.Reset(), "Reset failed");

  RequireNear(GetSimTime(simulation),
      0.0,
      SimTimeTolerance,
      "Reset did not reset simulation time");

  const sim::InitialCondition captured = simulation.GetCurrentCondition();
  const sim::InitialCondition &defaultInitialCondition =
      simulation.GetDefaultInitialCondition();
  RequireNear(captured.altitudeFt,
      defaultInitialCondition.altitudeFt,
      AltitudeToleranceFt,
      "Reset altitude does not match default IC");
  RequireNear(captured.airspeedKts,
      defaultInitialCondition.airspeedKts,
      AirspeedToleranceKts,
      "Reset airspeed does not match default IC");
}

void TestResetWithInitialCondition() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);

  sim::InitialCondition initialCondition =
      simulation.GetDefaultInitialCondition();
  initialCondition.altitudeFt = 2500.0;
  initialCondition.headingDeg = 45.0;
  initialCondition.airspeedKts = 95.0;

  Require(simulation.Reset(initialCondition), "Reset with custom IC failed");

  const sim::InitialCondition captured = simulation.GetCurrentCondition();
  RequireNear(captured.altitudeFt,
      initialCondition.altitudeFt,
      AltitudeToleranceFt,
      "Custom reset altitude mismatch");
  RequireNear(captured.headingDeg,
      initialCondition.headingDeg,
      HeadingToleranceDeg,
      "Custom reset heading mismatch");
  RequireNear(captured.airspeedKts,
      initialCondition.airspeedKts,
      AirspeedToleranceKts,
      "Custom reset airspeed mismatch");

  Require(simulation.Reset(), "Default reset after custom reset failed");
  const sim::InitialCondition restoredDefault =
      simulation.GetCurrentCondition();
  RequireNear(restoredDefault.altitudeFt,
      simulation.GetDefaultInitialCondition().altitudeFt,
      AltitudeToleranceFt,
      "Custom reset replaced the default altitude");
  RequireNear(restoredDefault.airspeedKts,
      simulation.GetDefaultInitialCondition().airspeedKts,
      AirspeedToleranceKts,
      "Custom reset replaced the default airspeed");
}

void TestCaptureCurrentStateCanReset() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  Require(simulation.Tick(), "Tick before capture failed");

  const sim::InitialCondition captured = simulation.GetCurrentCondition();
  Require(simulation.Reset(captured), "Reset with captured state failed");
  const sim::InitialCondition restored = simulation.GetCurrentCondition();

  RequireNear(restored.altitudeFt,
      captured.altitudeFt,
      AltitudeToleranceFt,
      "Captured reset altitude mismatch");
  RequireNear(restored.headingDeg,
      captured.headingDeg,
      HeadingToleranceDeg,
      "Captured reset heading mismatch");
  RequireNear(restored.airspeedKts,
      captured.airspeedKts,
      AirspeedToleranceKts,
      "Captured reset airspeed mismatch");
}

void TestEngineStateInspection() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  const auto &aircraft = simulation.GetAircraft();
  const auto &engines = aircraft.GetEngines();
  const std::size_t engineCount = engines.GetEngineCount();

  Require(engineCount >= 1, "Expected at least one engine");

  const sim::EngineState engineState = engines.GetEngineState(0);

  Require(engineState.index == 0, "Engine index mismatch");
  if (engineCount == 1) {
    Require(engineState.running == engines.IsAnyEngineRunning(),
        "Single engine running state differs from aggregate query");
    Require(engineState.running == engines.AreAllEnginesRunning(),
        "Single engine running state differs from all-engines query");
  }
  Require(std::isfinite(engineState.rpm), "Engine RPM is not finite");
  Require(std::isfinite(engineState.throttleCommand),
      "Engine throttle command is not finite");

  const sim::EngineState invalidEngineState =
      engines.GetEngineState(engineCount + 1);
  Require(invalidEngineState.index == engineCount + 1,
      "Invalid engine index was not preserved");
}

void TestInvalidInitialConditionFails() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  sim::InitialCondition invalid = simulation.GetDefaultInitialCondition();
  invalid.latitudeDeg = 100.0;

  std::string validationError;
  Require(!sim::ValidateInitialCondition(invalid, &validationError),
      "Standalone validation accepted invalid latitude");
  Require(validationError
              == "Latitude must be finite and between -90 and 90 degrees.",
      "Standalone validation returned a different error");

  Require(!simulation.Reset(invalid), "Invalid latitude was accepted");
  Require(simulation.GetErrorTracker().GetLastError().has_value(),
      "Invalid IC did not report an error");
}

void TestAircraftStateAccess() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  const sim::Aircraft &aircraft = simulation.GetAircraft();
  const sim::AircraftState aircraftState = aircraft.GetAircraftState();
  const sim::AircraftStateDerivative derivative =
      aircraft.GetAircraftStateDerivative();
  const sim::InitialCondition currentCondition = aircraft.GetCurrentCondition();
  const auto &properties = aircraft.GetProperties();

  Require(std::isfinite(currentCondition.altitudeFt),
      "Aircraft altitude invalid");
  Require(std::isfinite(aircraftState.trueAirspeedMps),
      "Aircraft airspeed invalid");
  Require(std::isfinite(aircraftState.alphaDeg),
      "Aircraft state alpha invalid");
  Require(std::isfinite(aircraftState.betaDeg), "Aircraft state beta invalid");
  Require(std::isfinite(derivative.uDotMps2),
      "Aircraft state derivative uDot invalid");
  RequireNear(properties.Roll().Rad(),
      math::DegToRad(aircraftState.rollDeg),
      SimTimeTolerance,
      "Flight properties roll rad mismatch");
  RequireNear(properties.Pitch().Rad(),
      math::DegToRad(aircraftState.pitchDeg),
      SimTimeTolerance,
      "Flight properties pitch rad mismatch");
  RequireNear(properties.P().RadPerSec(),
      math::DegToRad(aircraftState.pDegPerSec),
      SimTimeTolerance,
      "Flight properties roll rate rad mismatch");
  RequireNear(properties.P().DotRadPerSec2(),
      math::DegToRad(derivative.pDotDegPerSec2),
      SimTimeTolerance,
      "Flight properties roll acceleration rad mismatch");
  RequireNear(properties.TrueAirspeed().Mps(),
      aircraftState.trueAirspeedMps,
      SimTimeTolerance,
      "Flight properties true airspeed mps mismatch");
  RequireNear(properties.AltitudeAgl().Ft(),
      aircraftState.altitudeAglFt,
      SimTimeTolerance,
      "Aircraft state AGL altitude mismatch");
  RequireNear(properties.U().Mps(),
      aircraftState.uMps,
      SimTimeTolerance,
      "Flight properties U mps mismatch");
  RequireNear(properties.U().DotMps2(),
      derivative.uDotMps2,
      SimTimeTolerance,
      "Flight properties U acceleration mismatch");
  RequireNear(properties.VerticalSpeed().FtPerMin(),
      properties.VerticalSpeed().Fps() * 60.0,
      SimTimeTolerance,
      "Flight properties vertical speed ft/min mismatch");
  Require(std::isfinite(properties.Alpha().Rad()),
      "Flight properties alpha rad invalid");
  Require(std::isfinite(properties.Beta().Rad()),
      "Flight properties beta rad invalid");
}

void TestNavigationProperties() {
  sim::Aircraft aircraft;
  sim::InitialCondition initialCondition{};
  initialCondition.headingDeg = 0.0;
  initialCondition.airspeedKts = 100.0;
  Require(aircraft.Initialize(MakeConfig(), initialCondition),
      "Aircraft failed to initialize for navigation property test");

  sim::FDMState state = aircraft.ExtractFDMState(sim::FDMStateFlags::State);
  state.state.bodyVelocityFps = {120.0, 80.0, 0.0};
  state.state.attitudeRad = {0.0, 0.0, 0.0};
  aircraft.ApplyFDMState(state);
  Require(aircraft.Tick(), "Navigation property test tick failed");

  const sim::jsbsim::Properties &properties = aircraft.GetProperties();
  const double northVelocityFps = properties.NorthVelocity().Fps();
  const double eastVelocityFps = properties.EastVelocity().Fps();
  const double expectedGroundSpeedFps =
      std::hypot(northVelocityFps, eastVelocityFps);
  RequireNear(properties.GroundSpeed().Fps(),
      expectedGroundSpeedFps,
      SimTimeTolerance,
      "Ground speed does not match horizontal navigation velocity");
  RequireNear(properties.GroundSpeed().Mps(),
      expectedGroundSpeedFps * 0.3048,
      SimTimeTolerance,
      "Ground speed metric conversion mismatch");

  const double expectedCourseRad =
      std::atan2(eastVelocityFps, northVelocityFps);
  RequireNear(properties.Course().Rad(),
      expectedCourseRad,
      SimTimeTolerance,
      "Course does not match horizontal ground-track direction");
  const sim::AircraftState aircraftState = aircraft.GetAircraftState();
  const double expectedCourseDeg =
      math::Wrap(math::RadToDeg(expectedCourseRad), 0.0, 360.0);
  RequireNear(aircraftState.courseDeg,
      expectedCourseDeg,
      SimTimeTolerance,
      "Aircraft state course is not normalized ground track");
  const double headingRad = math::DegToRad(aircraftState.headingDeg);
  Require(std::fabs(properties.Course().Rad() - headingRad) > 0.1,
      "Course accessor returned aircraft heading instead of ground track");

  const double gravityMps2 = properties.GravityMps2();
  Require(gravityMps2 > 8.0 && gravityMps2 < 11.0,
      "Local gravitational acceleration is not physically reasonable");
}

void TestStartAppliesInitialTrim() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  const auto &aircraft = simulation.GetAircraft();
  const auto &controls = aircraft.GetControls();
  const control::ControlInput &input = controls.GetInput();
  const double pitchTrim = controls.GetPitchTrim();

  Require(input.throttle > TrimInputTolerance,
      "Initial trim did not apply throttle command input");
  Require(std::fabs(pitchTrim) > TrimInputTolerance,
      "Initial trim did not apply pitch trim");
  RequireNear(GetSimTime(simulation),
      0.0,
      SimTimeTolerance,
      "Initial trim changed simulation time");
}

void TestInitialTrimIsStoredInSimulation() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  const gnc::TrimResult *trimResult = simulation.GetTrimService().GetResult();

  Require(trimResult != nullptr,
      "Simulation did not store initial trim result");
  Require(trimResult->success,
      "Simulation stored a failed initial trim result");
}

void TestSimulationUsesInjectedAutopilot() {
  auto injectedAutopilot = std::make_unique<gnc::MyAutopilot>();
  gnc::IAutopilot *expectedAutopilot = injectedAutopilot.get();
  sim::Simulation simulation(std::move(injectedAutopilot));

  Require(&GetFlightControlManager(simulation).GetAutopilot()
              == expectedAutopilot,
      "Simulation did not preserve the injected Autopilot instance");
}

void TestPx4AutopilotOwnsBaselineRollReference() {
  sim::Simulation simulation(std::make_unique<gnc::PX4Autopilot>());
  StartSimulation(simulation);

  auto &manager = GetFlightControlManager(simulation);
  auto *autopilot = dynamic_cast<gnc::PX4Autopilot *>(&manager.GetAutopilot());
  Require(autopilot != nullptr,
      "Simulation did not preserve the injected PX4Autopilot instance");
  auto *rollReference =
      autopilot->GetController<gnc::Px4RollHoldReferenceController>();
  Require(rollReference != nullptr,
      "PX4 baseline is missing its Roll Hold reference controller");
  Require(autopilot->GetController<gnc::RollHoldController>() == nullptr,
      "PX4 baseline unexpectedly owns the custom Roll Hold controller");

  const control::ControlInput passthrough{
      .elevator = -0.12,
      .aileron = 0.23,
      .rudder = -0.04,
      .throttle = 0.61,
  };
  manager.GetManualController().SetCommandedInput(passthrough);
  manager.SetMode(control::FlightControlMode::Autopilot);
  Require(simulation.Tick(), "Disabled PX4 baseline tick failed");
  Require(simulation.GetAircraft().GetControls().GetInput() == passthrough,
      "Disabled PX4 baseline did not preserve pass-through control");

  gnc::Px4RollHoldReferenceSettings settings = autopilot->GetRollHoldSettings();
  settings.timeConstantSec = 0.72;
  settings.rateProportionalGain = 0.21;
  autopilot->SetRollHoldSettings(settings);
  RequireNear(autopilot->GetRollHoldSettings().timeConstantSec,
      0.72,
      SimTimeTolerance,
      "PX4 baseline did not retain Advanced time-constant tuning");
  RequireNear(autopilot->GetRollHoldSettings().rateProportionalGain,
      0.21,
      SimTimeTolerance,
      "PX4 baseline did not retain Advanced proportional tuning");

  const double targetRollRad =
      simulation.GetAircraft().GetProperties().Roll().Rad() + 0.2;
  autopilot->SetTargetRollRad(targetRollRad);
  autopilot->SetRollHoldEnabled(true);
  Require(simulation.Tick(), "Enabled PX4 baseline tick failed");
  const auto &diagnostics = autopilot->GetRollHoldDiagnostics();
  const control::ControlInput actualInput =
      simulation.GetAircraft().GetControls().GetInput();
  RequireNear(actualInput.aileron,
      diagnostics.aileronCommand,
      ControlCommandTolerance,
      "Advanced PX4 tuning did not drive the baseline aileron output");
  RequireNear(diagnostics.targetRollRad,
      targetRollRad,
      SimTimeTolerance,
      "PX4 baseline did not use its Roll Hold target");
  const auto &telemetry = simulation.GetTelemetryRegistry();
  const auto *commandedRoll =
      telemetry.Find(telemetry::paths::AutopilotRollHoldCommandedRoll);
  const auto *aileronCommand =
      telemetry.Find(telemetry::paths::AutopilotRollHoldAileronCommand);
  const auto *commandedRollRate =
      telemetry.Find(telemetry::paths::AutopilotRollHoldCommandedRollRate);
  const auto *rollRate =
      telemetry.Find(telemetry::paths::AutopilotRollHoldRollRate);
  const auto *rollRateError =
      telemetry.Find(telemetry::paths::AutopilotRollHoldRollRateError);
  Require(commandedRoll != nullptr && aileronCommand != nullptr
              && commandedRollRate != nullptr && rollRate != nullptr
              && rollRateError != nullptr,
      "PX4 baseline did not publish generic Roll Hold telemetry");
  RequireNear(commandedRoll->GetLatest()->value,
      math::RadToDeg(targetRollRad),
      SimTimeTolerance,
      "PX4 baseline telemetry did not retain its Roll Hold target");
  RequireNear(aileronCommand->GetLatest()->value,
      diagnostics.aileronCommand,
      ControlCommandTolerance,
      "PX4 baseline telemetry did not retain its aileron command");
  RequireNear(commandedRollRate->GetLatest()->value,
      math::RadToDeg(diagnostics.bodyRateSetpointRadPerSec),
      SimTimeTolerance,
      "PX4 baseline telemetry did not adapt its commanded roll rate");
  RequireNear(rollRate->GetLatest()->value,
      math::RadToDeg(diagnostics.bodyRateSetpointRadPerSec
                     - diagnostics.bodyRateErrorRadPerSec),
      SimTimeTolerance,
      "PX4 baseline telemetry did not retain its measured roll rate");
  RequireNear(rollRateError->GetLatest()->value,
      math::RadToDeg(diagnostics.bodyRateErrorRadPerSec),
      SimTimeTolerance,
      "PX4 baseline telemetry did not adapt its roll-rate error");
  const auto requireDiagnosticTelemetry = [&telemetry](std::string_view path,
                                              double expected) {
    const telemetry::TelemetryChannel *channel = telemetry.Find(path);
    Require(channel != nullptr && channel->GetLatest() != nullptr,
        "PX4 baseline diagnostic telemetry channel is missing");
    RequireNear(channel->GetLatest()->value,
        expected,
        ControlCommandTolerance,
        "PX4 baseline diagnostic telemetry value mismatch");
  };
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRateProportionalTerm,
      diagnostics.rateProportionalTerm);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRateIntegralTerm,
      diagnostics.rateIntegralTerm);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRateDerivativeTerm,
      diagnostics.rateDerivativeTerm);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRateFeedForwardTerm,
      diagnostics.rateFeedForwardTerm);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldUnscaledTorqueCommand,
      diagnostics.unscaledTorqueCommand);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRawTorqueCommand,
      diagnostics.rawTorqueCommand);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRollTorqueCommand,
      diagnostics.rollTorqueCommand);
  requireDiagnosticTelemetry(telemetry::paths::AutopilotRollHoldAirspeedScaling,
      diagnostics.airspeedScaling);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldPositiveSaturation,
      diagnostics.positiveSaturation ? 1.0 : 0.0);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldNegativeSaturation,
      diagnostics.negativeSaturation ? 1.0 : 0.0);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldIntegratorLimited,
      diagnostics.integratorLimited ? 1.0 : 0.0);
  requireDiagnosticTelemetry(telemetry::paths::AutopilotRollHoldTrimRollCommand,
      diagnostics.trimRollCommand);
  const double integratorLimit =
      std::abs(autopilot->GetRollHoldSettings().integratorLimit);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRateIntegratorPositiveLimit,
      integratorLimit);
  requireDiagnosticTelemetry(
      telemetry::paths::AutopilotRollHoldRateIntegratorNegativeLimit,
      -integratorLimit);
  RequireNear(actualInput.elevator,
      passthrough.elevator,
      ControlCommandTolerance,
      "PX4 baseline changed the passthrough elevator");
  RequireNear(actualInput.rudder,
      passthrough.rudder,
      ControlCommandTolerance,
      "PX4 baseline changed the passthrough rudder");
  RequireNear(actualInput.throttle,
      passthrough.throttle,
      ControlCommandTolerance,
      "PX4 baseline changed the passthrough throttle");
}

void TestAutopilotControllerRegistry() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &autopilot = dynamic_cast<gnc::MyAutopilot &>(
      GetFlightControlManager(simulation).GetAutopilot());

  Require(autopilot.GetController<gnc::RollHoldController>() != nullptr,
      "Autopilot is missing RollHoldController");
  Require(autopilot.GetController<gnc::Px4RollHoldReferenceController>()
              == nullptr,
      "MyAutopilot still registers Px4RollHoldReferenceController");
  Require(autopilot.GetController<gnc::PitchHoldController>() == nullptr,
      "Autopilot still registers PitchHoldController");
  Require(autopilot.GetController<gnc::AirspeedHoldController>() == nullptr,
      "Autopilot still registers AirspeedHoldController");
  Require(autopilot.GetController<gnc::CourseHoldController>() == nullptr,
      "Autopilot still registers CourseHoldController");
  Require(autopilot.GetController<gnc::AltitudeHoldController>() == nullptr,
      "Autopilot still registers AltitudeHoldController");
  Require(autopilot.GetController<gnc::YawDamperController>() == nullptr,
      "Autopilot still registers YawDamperController");

  const gnc::MyAutopilot &constAutopilot = autopilot;
  Require(constAutopilot.GetController<gnc::RollHoldController>() != nullptr,
      "Const controller lookup failed");

  int resetCount = 0;
  auto *controller =
      autopilot.AddController<RegistryTestController>(resetCount);
  Require(controller != nullptr, "Failed to add controller to Autopilot");
  Require(autopilot.GetController<RegistryTestController>() == controller,
      "Controller lookup did not return the registered controller");

  autopilot.Reset();
  Require(resetCount == 1,
      "Autopilot did not reset a registered controller generically");
  Require(autopilot.RemoveController<RegistryTestController>(),
      "Failed to remove controller from Autopilot");
  Require(autopilot.GetController<RegistryTestController>() == nullptr,
      "Removed controller is still registered");
}

void TestControlSystemAxisSettersClampFinalInput() {
  sim::Aircraft aircraft;
  auto &controls = aircraft.GetControls();

  Require(controls.SetElevator(-2.0), "Elevator setter did not change");
  Require(controls.SetAileron(2.0), "Aileron setter did not change");
  Require(controls.SetRudder(3.0), "Rudder setter did not change");
  Require(controls.SetThrottle(0.5), "Throttle setter did not change");
  Require(controls.SetThrottle(-1.0), "Throttle setter did not change");

  const control::ControlInput &input = controls.GetInput();
  RequireNear(input.elevator,
      -1.0,
      SimTimeTolerance,
      "Elevator setter did not clamp lower bound");
  RequireNear(input.aileron,
      1.0,
      SimTimeTolerance,
      "Aileron setter did not clamp upper bound");
  RequireNear(input.rudder,
      1.0,
      SimTimeTolerance,
      "Rudder setter did not clamp upper bound");
  RequireNear(input.throttle,
      0.0,
      SimTimeTolerance,
      "Throttle setter did not clamp lower bound");
}

void TestControlSystemSetInputClampsFinalInput() {
  sim::Aircraft aircraft;
  auto &controls = aircraft.GetControls();

  controls.SetInput({
      .elevator = -2.0,
      .aileron = 2.0,
      .rudder = 3.0,
      .throttle = 2.0,
  });

  const control::ControlInput &input = controls.GetInput();
  RequireNear(input.elevator,
      -1.0,
      SimTimeTolerance,
      "Aircraft input did not clamp elevator lower bound");
  RequireNear(input.aileron,
      1.0,
      SimTimeTolerance,
      "Aircraft input did not clamp aileron upper bound");
  RequireNear(input.rudder,
      1.0,
      SimTimeTolerance,
      "Aircraft input did not clamp rudder upper bound");
  RequireNear(input.throttle,
      1.0,
      SimTimeTolerance,
      "Aircraft input did not clamp throttle upper bound");
}

void TestManualFlightControlControllerAppliesCommands() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &aircraft = simulation.GetAircraft();
  auto &flightControlManager = GetFlightControlManager(simulation);
  auto &manualController = flightControlManager.GetManualController();

  flightControlManager.SetMode(control::FlightControlMode::Manual);
  manualController.SetCommandedInput({
      .elevator = 0.25,
      .aileron = 2.0,
      .rudder = -0.25,
      .throttle = 0.5,
  });

  const control::ControlInput &commandedInput =
      manualController.GetCommandedInput();
  RequireNear(commandedInput.throttle,
      0.5,
      SimTimeTolerance,
      "Throttle command mismatch");
  RequireNear(commandedInput.aileron,
      1.0,
      SimTimeTolerance,
      "Manual controller should clamp commanded aileron");
  RequireNear(commandedInput.elevator,
      0.25,
      SimTimeTolerance,
      "Elevator command mismatch");
  RequireNear(commandedInput.rudder,
      -0.25,
      SimTimeTolerance,
      "Rudder command mismatch");

  Require(simulation.Tick(), "Manual flight control tick failed");

  const control::ControlInput &actualInput = aircraft.GetControls().GetInput();
  RequireNear(actualInput.throttle,
      0.5,
      SimTimeTolerance,
      "Throttle command was not applied");
  RequireNear(actualInput.aileron,
      1.0,
      SimTimeTolerance,
      "Aileron command was not applied");
  RequireNear(actualInput.elevator,
      0.25,
      SimTimeTolerance,
      "Elevator command was not applied");
  RequireNear(actualInput.rudder,
      -0.25,
      SimTimeTolerance,
      "Rudder command was not applied");
}

void TestFlightControlManagerOwnsAndRoutesControllers() {
  const control::ControlInput manualInput{
      .elevator = 0.1,
      .aileron = 0.2,
      .rudder = 0.3,
      .throttle = 0.4,
  };
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &manager = GetFlightControlManager(simulation);
  const auto &aircraft = simulation.GetAircraft();

  manager.GetManualController().SetCommandedInput(manualInput);
  Require(simulation.Tick(), "Manual manager routing tick failed");
  Require(aircraft.GetControls().GetInput() == manualInput,
      "Manager did not route its manual controller output");

  manager.SetMode(control::FlightControlMode::Autopilot);
  Require(simulation.Tick(), "Autopilot manager routing tick failed");
  Require(aircraft.GetControls().GetInput() == manualInput,
      "Autopilot did not preserve manual pass-through output");
}

void TestFlightControlManagerNoInputPreservesCommand() {
  const control::ControlInput retainedInput{
      .elevator = 0.1,
      .aileron = -0.2,
      .rudder = 0.3,
      .throttle = 0.4,
  };
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &manager = GetFlightControlManager(simulation);
  auto &controls = simulation.GetAircraft().GetControls();

  controls.SetInput(retainedInput);
  manager.SetMode(control::FlightControlMode::None);
  Require(simulation.Tick(), "No-input manager tick failed");
  Require(controls.GetInput() == retainedInput,
      "No-input mode replaced the existing control command");

  manager.GetManualController().SetCommandedInput({});
  manager.SetMode(control::FlightControlMode::Manual);
  Require(simulation.Tick(), "Explicit-zero manager tick failed");
  Require(controls.GetInput() == control::ControlInput{},
      "Explicit zero command was treated as no control update");
}

void TestManualModeIgnoresAutopilotSource() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &aircraft = simulation.GetAircraft();
  auto &flightControlManager = GetFlightControlManager(simulation);
  auto &manualController = flightControlManager.GetManualController();
  auto &autopilot = GetMyAutopilot(simulation);

  flightControlManager.SetMode(control::FlightControlMode::Manual);
  manualController.SetCommandedInput({
      .elevator = 0.2,
      .aileron = -0.8,
      .rudder = 0.1,
      .throttle = 0.4,
  });

  const auto &properties = aircraft.GetProperties();
  autopilot.SetRollHoldSettings({
      .targetRollRad = properties.Roll().Rad() + 0.1,
      .attitudeLoop = {.proportionalGain = 1.2},
      .rateLoop = {.proportionalGain = 0.4},
  });
  autopilot.SetRollHoldEnabled(true);

  Require(simulation.Tick(), "Manual mode tick failed");

  const control::ControlInput &actualInput = aircraft.GetControls().GetInput();
  RequireNear(actualInput.elevator,
      0.2,
      SimTimeTolerance,
      "Manual mode did not apply manual elevator");
  RequireNear(actualInput.aileron,
      -0.8,
      SimTimeTolerance,
      "Manual mode should ignore autopilot aileron");
  RequireNear(actualInput.rudder,
      0.1,
      SimTimeTolerance,
      "Manual mode did not apply manual rudder");
  RequireNear(actualInput.throttle,
      0.4,
      SimTimeTolerance,
      "Manual mode did not apply manual throttle");
}

void TestAutomaticLinearizationToggle() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  auto &flightControlManager = GetFlightControlManager(simulation);
  auto &autopilot = GetMyAutopilot(simulation);
  Require(autopilot.IsAutomaticLinearizationEnabled(),
      "Automatic linearization was not enabled by default");

  autopilot.SetAutomaticLinearizationEnabled(false);
  Require(!autopilot.IsAutomaticLinearizationEnabled(),
      "Automatic linearization did not turn off");
  autopilot.UpdateLinearization(simulation.GetAircraft(),
      sim::Tick{.simTimeSec = 100.0});
  Require(!autopilot.IsLinearizationInProgress()
              && autopilot.GetLinearizationResult() == nullptr,
      "Disabled automatic linearization scheduled an update");

  flightControlManager.ResetControllers();
  Require(!autopilot.IsAutomaticLinearizationEnabled(),
      "Controller reset unexpectedly enabled automatic linearization");

  autopilot.SetAutomaticLinearizationEnabled(true);
  Require(autopilot.IsAutomaticLinearizationEnabled(),
      "Automatic linearization did not turn back on");
}

void TestLinearizationRunsInManualModeWithoutHolds() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &flightControlManager = GetFlightControlManager(simulation);
  auto &autopilot = GetMyAutopilot(simulation);
  const control::ControlInput manualInput{
      .elevator = 0.2,
      .aileron = -0.3,
      .rudder = 0.1,
      .throttle = 0.4,
  };

  flightControlManager.SetMode(control::FlightControlMode::Manual);
  flightControlManager.GetManualController().SetCommandedInput(manualInput);
  Require(autopilot.IsAutomaticLinearizationEnabled(),
      "Automatic linearization was not enabled by default");
  Require(!autopilot.IsRollHoldEnabled(),
      "Always-on linearization test unexpectedly has an active Hold");

  Require(simulation.Tick(), "Linearization scheduling tick failed");
  const double firstRequestDueSec =
      GetSimTime(simulation) + ExpectedLinearizationRefreshIntervalSec;
  while (GetSimTime(simulation) < firstRequestDueSec) {
    Require(simulation.Tick(),
        "Simulation tick failed before periodic linearization request");
  }

  const auto kickoffStart = std::chrono::steady_clock::now();
  Require(simulation.Tick(), "Manual-mode linearization kickoff tick failed");
  const double kickoffDurationSec = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - kickoffStart)
                                        .count();
  Require(kickoffDurationSec < MaximumAsyncKickoffSec,
      "Manual-mode linearization blocked the simulation tick");

  WaitForLinearizationResult(simulation, autopilot);
  Require(autopilot.GetLinearizationResult() != nullptr,
      "Manual mode did not publish an asynchronous linearization result");
  const gnc::DynamicModeHistory &dynamicModeHistory =
      autopilot.GetDynamicModeHistory();
  Require(dynamicModeHistory.GetSnapshots().size() == 1,
      "Successful linearization did not append a dynamic-mode snapshot");
  const gnc::DynamicModeSnapshot &snapshot =
      dynamicModeHistory.GetSnapshots().front();
  Require(snapshot.analysis.valid,
      "Autopilot stored an invalid dynamic-mode snapshot");
  Require(snapshot.simulationTimeSec <= GetSimTime(simulation),
      "Dynamic-mode snapshot used a future simulation time");
  Require(dynamicModeHistory.FindLatestAtOrBefore(
              snapshot.simulationTimeSec - SimTimeTolerance)
              == nullptr,
      "Dynamic-mode history selected a future snapshot");
  Require(dynamicModeHistory.FindLatestAtOrBefore(snapshot.simulationTimeSec)
              == &snapshot,
      "Dynamic-mode history did not select the current snapshot");
  Require(flightControlManager.GetMode() == control::FlightControlMode::Manual,
      "Linearization changed the active flight-control mode");
  Require(simulation.GetAircraft().GetControls().GetInput() == manualInput,
      "Background linearization changed the manual control input");

  autopilot.SetAutomaticLinearizationEnabled(false);
  Require(autopilot.GetLinearizationResult() != nullptr,
      "Disabling automatic updates discarded the latest linearization");

  flightControlManager.ResetControllers();
  Require(!autopilot.IsAutomaticLinearizationEnabled(),
      "Controller reset unexpectedly enabled automatic linearization");
  Require(dynamicModeHistory.GetSnapshots().empty(),
      "Controller reset retained stale dynamic-mode snapshots");
}

void TestRollHoldControllerHasNoControlLaw() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &aircraft = simulation.GetAircraft();
  auto *rollHold =
      GetMyAutopilot(simulation).GetController<gnc::RollHoldController>();
  Require(rollHold != nullptr, "Autopilot is missing RollHoldController");

  const gnc::ControlContext emptyContext{};
  rollHold->SetEnabled(false);
  Require(!rollHold->OnTick(aircraft, MakeTestTick(simulation), emptyContext)
              .has_value(),
      "Disabled roll hold should not produce aileron command");

  const auto &properties = aircraft.GetProperties();
  const gnc::RollHoldSettings settings{
      .targetRollRad = properties.Roll().Rad() + 0.2,
      .attitudeLoop = {.proportionalGain = 1.2},
      .rateLoop = {.proportionalGain = 0.4},
  };
  rollHold->SetTrimAileron(0.1);
  rollHold->SetSettings(settings);
  rollHold->SetEnabled(true);

  const auto command =
      rollHold->OnTick(aircraft, MakeTestTick(simulation), emptyContext);
  Require(!command.has_value(),
      "Unimplemented Roll Hold unexpectedly produced an aileron command");

  const gnc::RollHoldDiagnostics &diagnostics = rollHold->GetDiagnostics();
  Require(!diagnostics.controlOutputValid,
      "Unimplemented Roll Hold marked its output valid");
  RequireNear(diagnostics.commandedRollRad,
      settings.targetRollRad,
      SimTimeTolerance,
      "Roll Hold diagnostics did not retain the commanded roll");
  RequireNear(diagnostics.rollRad,
      properties.Roll().Rad(),
      SimTimeTolerance,
      "Roll Hold diagnostics did not retain the measured roll");
  RequireNear(diagnostics.rollErrorRad,
      settings.targetRollRad - properties.Roll().Rad(),
      SimTimeTolerance,
      "Roll Hold diagnostics did not retain the roll error");
  RequireNear(diagnostics.rollRateRadPerSec,
      properties.P().RadPerSec(),
      SimTimeTolerance,
      "Roll Hold diagnostics did not retain the measured roll rate");
  Require(!diagnostics.commandedRollRateValid,
      "Unimplemented Roll Hold produced a commanded roll rate");
  RequireNear(diagnostics.commandedRollRateRadPerSec,
      0.0,
      SimTimeTolerance,
      "Invalid commanded roll-rate diagnostic was not reset");
  RequireNear(diagnostics.rollRateErrorRadPerSec,
      0.0,
      SimTimeTolerance,
      "Unimplemented Roll Hold produced a roll-rate error");
  RequireNear(diagnostics.aileronCommand,
      0.0,
      SimTimeTolerance,
      "Unimplemented Roll Hold produced a diagnostic aileron command");
  RequireNear(rollHold->GetSettings().attitudeLoop.proportionalGain,
      1.2,
      SimTimeTolerance,
      "Roll angle P gain was not retained");
  RequireNear(rollHold->GetSettings().rateLoop.proportionalGain,
      0.4,
      SimTimeTolerance,
      "Roll rate P gain was not retained");
  RequireNear(rollHold->GetTrimAileron(),
      0.1,
      SimTimeTolerance,
      "Roll Hold did not retain its trim reference");

  const double commandedRollRad = properties.Roll().Rad() - 0.15;
  const auto cascadedCommand = rollHold->OnTick(aircraft,
      MakeTestTick(simulation),
      emptyContext,
      commandedRollRad);
  Require(!cascadedCommand.has_value(),
      "Unimplemented cascaded Roll Hold produced a command");
  RequireNear(rollHold->GetDiagnostics().commandedRollRad,
      commandedRollRad,
      SimTimeTolerance,
      "Roll Hold diagnostics did not retain the cascaded roll command");
  RequireNear(rollHold->GetSettings().targetRollRad,
      settings.targetRollRad,
      SimTimeTolerance,
      "Outer-loop roll command replaced the standalone Roll Hold target");

  rollHold->SetEnabled(false);
  Require(!rollHold->GetDiagnostics().controlOutputValid,
      "Disabling Roll Hold retained active diagnostics");
  rollHold->Reset();
  RequireNear(rollHold->GetDiagnostics().commandedRollRad,
      0.0,
      SimTimeTolerance,
      "Roll Hold reset retained a stale commanded-roll diagnostic");
  RequireNear(rollHold->GetDiagnostics().aileronCommand,
      0.0,
      SimTimeTolerance,
      "Roll Hold reset retained a stale diagnostic command");
  Require(!rollHold->GetDiagnostics().controlOutputValid,
      "Roll Hold reset retained valid diagnostics");
}

void TestUnimplementedPrimaryRollHoldPublishesNoControlTelemetry() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &autopilot = GetMyAutopilot(simulation);
  auto &manager = GetFlightControlManager(simulation);
  const auto &properties = simulation.GetAircraft().GetProperties();

  const gnc::RollHoldSettings settings{
      .targetRollRad = properties.Roll().Rad() + 0.12,
      .attitudeLoop = {.proportionalGain = 1.2},
      .rateLoop = {.proportionalGain = 0.4},
  };
  autopilot.SetRollHoldSettings(settings);
  autopilot.SetRollHoldEnabled(true);
  manager.SetMode(control::FlightControlMode::Autopilot);
  Require(simulation.Tick(), "Primary Roll Hold telemetry tick failed");

  const auto *controller = autopilot.GetController<gnc::RollHoldController>();
  Require(controller != nullptr,
      "Primary Roll Hold telemetry test is missing its controller");
  const gnc::RollHoldDiagnostics &diagnostics = controller->GetDiagnostics();
  Require(!diagnostics.controlOutputValid,
      "Unimplemented Primary Roll Hold produced a valid output");

  const telemetry::TelemetryRegistry &telemetry =
      simulation.GetTelemetryRegistry();
  const auto requireLatestNear = [&telemetry](std::string_view path,
                                     double expected,
                                     const std::string &message) {
    const telemetry::TelemetryChannel *channel = telemetry.Find(path);
    Require(channel != nullptr && channel->GetLatest() != nullptr,
        message + " channel is unavailable");
    RequireNear(channel->GetLatest()->value,
        expected,
        SimTimeTolerance,
        message);
  };

  requireLatestNear(telemetry::paths::AutopilotRollHoldCommandedRoll,
      math::RadToDeg(diagnostics.commandedRollRad),
      "Commanded-roll telemetry mismatch");
  Require(telemetry.Find(telemetry::paths::AutopilotRollHoldAileronCommand)
              == nullptr,
      "Unimplemented Roll Hold published aileron-command telemetry");
  Require(telemetry.Find(telemetry::paths::AutopilotRollHoldRoll) == nullptr,
      "Unimplemented Roll Hold published controller-state telemetry");
  Require(telemetry.Find(telemetry::paths::AutopilotRollHoldRollError)
              == nullptr,
      "Unimplemented Roll Hold published roll-error telemetry");
  Require(telemetry.Find(telemetry::paths::AutopilotRollHoldRollRate)
              == nullptr,
      "Unimplemented Roll Hold published roll-rate telemetry");
  Require(telemetry.Find(telemetry::paths::AutopilotRollHoldCommandedRollRate)
              == nullptr,
      "Unimplemented Roll Hold published a commanded roll rate");
  Require(telemetry.Find(telemetry::paths::AutopilotRollHoldRollRateError)
              == nullptr,
      "Unimplemented Roll Hold published a roll-rate error");
  constexpr std::array Px4DiagnosticPaths{
      telemetry::paths::AutopilotRollHoldRateProportionalTerm,
      telemetry::paths::AutopilotRollHoldRateIntegralTerm,
      telemetry::paths::AutopilotRollHoldRateDerivativeTerm,
      telemetry::paths::AutopilotRollHoldRateFeedForwardTerm,
      telemetry::paths::AutopilotRollHoldUnscaledTorqueCommand,
      telemetry::paths::AutopilotRollHoldRawTorqueCommand,
      telemetry::paths::AutopilotRollHoldRollTorqueCommand,
      telemetry::paths::AutopilotRollHoldAirspeedScaling,
      telemetry::paths::AutopilotRollHoldPositiveSaturation,
      telemetry::paths::AutopilotRollHoldNegativeSaturation,
      telemetry::paths::AutopilotRollHoldIntegratorLimited,
      telemetry::paths::AutopilotRollHoldTrimRollCommand,
      telemetry::paths::AutopilotRollHoldRateIntegratorPositiveLimit,
      telemetry::paths::AutopilotRollHoldRateIntegratorNegativeLimit,
  };
  for (const std::string_view path : Px4DiagnosticPaths) {
    Require(telemetry.Find(path) == nullptr,
        "Non-PX4 Roll Hold published PX4 diagnostic telemetry");
  }
}

void TestPx4RollHoldReferenceComputesBaselineCommand() {
  sim::Simulation simulation(std::make_unique<gnc::PX4Autopilot>());
  StartSimulation(simulation);
  auto &aircraft = simulation.GetAircraft();
  auto &autopilot = dynamic_cast<gnc::PX4Autopilot &>(
      GetFlightControlManager(simulation).GetAutopilot());
  auto *controller =
      autopilot.GetController<gnc::Px4RollHoldReferenceController>();
  Require(controller != nullptr,
      "Autopilot is missing Px4RollHoldReferenceController");

  const sim::Tick tick = MakeTestTick(simulation);
  const auto &properties = aircraft.GetProperties();
  const double targetRollRad = properties.Roll().Rad() + 0.2;

  controller->SetEnabled(false);
  Require(!controller->OnTick(aircraft, tick, targetRollRad).has_value(),
      "Disabled PX4 roll reference produced a command");

  controller->SetEnabled(true);
  const auto command = controller->OnTick(aircraft, tick, targetRollRad);
  Require(command.has_value(),
      "Enabled PX4 roll reference produced no command");

  const auto &settings = controller->GetSettings();
  const double expectedRateSetpoint = std::clamp(0.2 / settings.timeConstantSec,
      -settings.maximumRollRateRadPerSec,
      settings.maximumRollRateRadPerSec);
  const double expectedRateError =
      expectedRateSetpoint - properties.P().RadPerSec();
  const double expectedAirspeedScale =
      settings.trimAirspeedMps
      / std::max(properties.CalibratedAirspeed().Mps(),
          settings.stallAirspeedMps);
  const double expectedProportionalTerm =
      settings.rateProportionalGain * expectedRateError;
  const double expectedIntegralTerm = 0.0;
  const double expectedDerivativeTerm =
      -settings.rateDerivativeGain * properties.P().DotRadPerSec2();
  const double expectedFeedForwardTerm = settings.rateFeedForwardGain
                                         / expectedAirspeedScale
                                         * expectedRateSetpoint;
  const double expectedUnscaledTorque =
      expectedProportionalTerm + expectedIntegralTerm + expectedDerivativeTerm
      + expectedFeedForwardTerm;
  const double expectedRawTorque =
      (expectedUnscaledTorque + settings.trimRollCommand)
      * expectedAirspeedScale * expectedAirspeedScale;
  const double expectedTorque = std::clamp(expectedRawTorque, -1.0, 1.0);

  RequireNear(*command,
      expectedTorque,
      1.0e-9,
      "PX4 C172x aileron mapping mismatch");
  const auto &diagnostics = controller->GetDiagnostics();
  Require(diagnostics.controlOutputValid,
      "PX4 Roll Hold diagnostics did not mark its output valid");
  RequireNear(diagnostics.targetRollRad,
      targetRollRad,
      1.0e-9,
      "PX4 roll reference did not retain target roll");
  RequireNear(diagnostics.bodyRateSetpointRadPerSec,
      expectedRateSetpoint,
      1.0e-9,
      "PX4 roll reference rate setpoint mismatch");
  RequireNear(diagnostics.airspeedScaling,
      expectedAirspeedScale,
      1.0e-9,
      "PX4 roll reference airspeed scaling mismatch");
  RequireNear(diagnostics.rateProportionalTerm,
      expectedProportionalTerm,
      1.0e-9,
      "PX4 roll reference proportional contribution mismatch");
  RequireNear(diagnostics.rateIntegralTerm,
      expectedIntegralTerm,
      1.0e-9,
      "PX4 roll reference integral contribution mismatch");
  RequireNear(diagnostics.rateDerivativeTerm,
      expectedDerivativeTerm,
      1.0e-9,
      "PX4 roll reference derivative contribution mismatch");
  RequireNear(diagnostics.rateFeedForwardTerm,
      expectedFeedForwardTerm,
      1.0e-9,
      "PX4 roll reference feed-forward contribution mismatch");
  RequireNear(diagnostics.unscaledTorqueCommand,
      expectedUnscaledTorque,
      1.0e-9,
      "PX4 roll reference unscaled torque mismatch");
  RequireNear(diagnostics.rawTorqueCommand,
      expectedRawTorque,
      1.0e-9,
      "PX4 roll reference raw torque mismatch");
  RequireNear(diagnostics.rollTorqueCommand,
      expectedTorque,
      1.0e-9,
      "PX4 roll reference saturated torque mismatch");
  RequireNear(diagnostics.trimRollCommand,
      settings.trimRollCommand,
      1.0e-9,
      "PX4 roll reference trim contribution mismatch");
  Require(diagnostics.positiveSaturation == (expectedRawTorque > 1.0),
      "PX4 roll reference positive saturation status mismatch");
  Require(diagnostics.negativeSaturation == (expectedRawTorque < -1.0),
      "PX4 roll reference negative saturation status mismatch");
  Require(!diagnostics.integratorLimited,
      "PX4 roll reference unexpectedly reported a limited integrator");
  Require(diagnostics.rateIntegrator != 0.0,
      "PX4 roll reference did not update its rate integrator");

  controller->Reset();
  Require(!controller->GetDiagnostics().controlOutputValid,
      "PX4 Roll Hold reset retained valid diagnostics");
  RequireNear(controller->GetDiagnostics().aileronCommand,
      0.0,
      1.0e-9,
      "PX4 roll reference reset retained a stale command");
  RequireNear(controller->GetDiagnostics().rateIntegrator,
      0.0,
      1.0e-9,
      "PX4 roll reference reset retained its integrator");
}

void TestPx4BaselineRollHoldControlsAircraft() {
  sim::Simulation simulation(std::make_unique<gnc::PX4Autopilot>());
  StartSimulation(simulation);
  auto &flightControlManager = GetFlightControlManager(simulation);
  auto &autopilot = dynamic_cast<gnc::PX4Autopilot &>(
      flightControlManager.GetAutopilot());
  auto &aircraft = simulation.GetAircraft();
  auto *px4Reference =
      autopilot.GetController<gnc::Px4RollHoldReferenceController>();
  Require(px4Reference != nullptr,
      "Baseline Roll Hold test is missing its controller");

  const gnc::TrimResult *trimResult = simulation.GetTrimService().GetResult();
  Require(trimResult != nullptr,
      "PX4 Roll Hold source test is missing the initial trim result");
  RequireNear(px4Reference->GetSettings().trimRollCommand,
      trimResult->aileron,
      1.0e-9,
      "PX4 Roll Hold did not synchronize the trim aileron");
  RequireNear(px4Reference->GetSettings().trimAirspeedMps,
      aircraft.GetProperties().CalibratedAirspeed().Mps(),
      1.0e-9,
      "PX4 Roll Hold did not synchronize the trim airspeed");

  const double targetRollRad =
      aircraft.GetProperties().Roll().Rad() + 0.15;
  const double initialRollRad = aircraft.GetProperties().Roll().Rad();
  autopilot.SetTargetRollRad(targetRollRad);
  autopilot.SetRollHoldEnabled(true);
  flightControlManager.SetMode(control::FlightControlMode::Autopilot);

  Require(px4Reference->IsEnabled(),
      "Selecting PX4 did not enable the PX4 Roll Hold controller");
  Require(simulation.Tick(), "PX4 Roll Hold control tick failed");
  RequireNear(aircraft.GetControls().GetAileron(),
      px4Reference->GetDiagnostics().aileronCommand,
      ControlCommandTolerance,
      "PX4 Roll Hold output was not applied to the aircraft");
  Require(px4Reference->GetDiagnostics().aileronCommand > trimResult->aileron,
      "Positive PX4 roll error did not increase positive-roll aileron");
  for (int tickIndex = 0; tickIndex < 60; ++tickIndex) {
    Require(simulation.Tick(), "PX4 Roll Hold response tick failed");
  }
  Require(aircraft.GetProperties().Roll().Rad() > initialRollRad,
      "PX4 Roll Hold moved the C172x opposite the positive roll target");

  double minimumSettledRollRad = 0.0;
  double maximumSettledRollRad = 0.0;
  int lastOutsideHalfDegreeTick = 60;
  constexpr int ResponseTickCount = 8 * 120;
  constexpr int SettledWindowTickCount = 2 * 120;
  for (int tickIndex = 60; tickIndex < ResponseTickCount; ++tickIndex) {
    Require(simulation.Tick(), "PX4 Roll Hold settling tick failed");
    const double rollErrorRad = std::fabs(
        targetRollRad - aircraft.GetProperties().Roll().Rad());
    if (rollErrorRad > math::DegToRad(0.5)) {
      lastOutsideHalfDegreeTick = tickIndex;
    }
    if (tickIndex >= ResponseTickCount - SettledWindowTickCount) {
      const double rollRad = aircraft.GetProperties().Roll().Rad();
      if (tickIndex == ResponseTickCount - SettledWindowTickCount) {
        minimumSettledRollRad = rollRad;
        maximumSettledRollRad = rollRad;
      } else {
        minimumSettledRollRad = std::min(minimumSettledRollRad, rollRad);
        maximumSettledRollRad = std::max(maximumSettledRollRad, rollRad);
      }
    }
  }

  const double finalRollErrorRad = std::fabs(
      targetRollRad - aircraft.GetProperties().Roll().Rad());
  const double settledRollRangeRad =
      maximumSettledRollRad - minimumSettledRollRad;
  const double halfDegreeSettlingTimeSec =
      static_cast<double>(lastOutsideHalfDegreeTick + 1) / 120.0;
  Require(halfDegreeSettlingTimeSec < 5.0,
      "PX4 Roll Hold settled too slowly; half_degree_settling_sec="
          + std::to_string(halfDegreeSettlingTimeSec));
  Require(finalRollErrorRad < math::DegToRad(0.5),
      "PX4 Roll Hold did not settle near its target; error_deg="
          + std::to_string(math::RadToDeg(finalRollErrorRad)));
  Require(settledRollRangeRad < math::DegToRad(0.5),
      "PX4 Roll Hold retained excessive settled oscillation; range_deg="
          + std::to_string(math::RadToDeg(settledRollRangeRad)));
  autopilot.SetRollHoldEnabled(false);
  Require(!px4Reference->IsEnabled(),
      "Disabling Baseline Roll Hold left its controller enabled");
}

void TestAutopilotModeWithUnimplementedRollHoldPassesThrough() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &aircraft = simulation.GetAircraft();
  auto &flightControlManager = GetFlightControlManager(simulation);
  auto &manualController = flightControlManager.GetManualController();
  auto &autopilot = GetMyAutopilot(simulation);

  manualController.SetCommandedInput({
      .elevator = 0.2,
      .aileron = -0.8,
      .rudder = 0.1,
      .throttle = 0.4,
  });

  const auto &properties = aircraft.GetProperties();
  const gnc::RollHoldSettings rollSettings{
      .targetRollRad = properties.Roll().Rad() + 0.1,
      .attitudeLoop = {.proportionalGain = 1.2},
      .rateLoop = {.proportionalGain = 0.4},
  };
  autopilot.SetRollHoldSettings(rollSettings);
  autopilot.SetRollHoldEnabled(true);
  flightControlManager.SetMode(control::FlightControlMode::Autopilot);

  const auto kickoffStart = std::chrono::steady_clock::now();
  Require(simulation.Tick(), "Asynchronous linearization kickoff tick failed");
  const double kickoffDurationSec = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - kickoffStart)
                                        .count();
  Require(kickoffDurationSec < MaximumAsyncKickoffSec,
      "Autopilot linearization blocked the simulation tick");
  Require(!autopilot.GetRollDynamics() && !autopilot.GetPitchDynamics()
              && !autopilot.GetYawDynamics(),
      "Autopilot unexpectedly published dynamics during kickoff");
  const control::ControlInput &kickoffInput = aircraft.GetControls().GetInput();
  RequireNear(kickoffInput.elevator,
      0.2,
      SimTimeTolerance,
      "Autopilot changed elevator before dynamics were ready");
  RequireNear(kickoffInput.aileron,
      -0.8,
      SimTimeTolerance,
      "Autopilot changed aileron before dynamics were ready");

  WaitForAutopilotDynamics(simulation, autopilot);
  const auto rollDynamics = autopilot.GetRollDynamics();
  const auto yawDynamics = autopilot.GetYawDynamics();
  Require(rollDynamics.has_value(), "Autopilot did not provide roll dynamics");
  Require(yawDynamics.has_value(), "Autopilot did not provide yaw dynamics");
  const gnc::LinearizationResult *linearization =
      autopilot.GetLinearizationResult();
  Require(linearization != nullptr,
      "Autopilot did not publish the periodic linearization result");
  Require(
      linearization->A.rows()
              == static_cast<Eigen::Index>(linearization->stateNames.size())
          && linearization->A.cols()
                 == static_cast<Eigen::Index>(linearization->stateNames.size()),
      "Periodic system matrix dimensions do not match state names");
  Require(
      linearization->B.rows()
              == static_cast<Eigen::Index>(linearization->stateNames.size())
          && linearization->B.cols()
                 == static_cast<Eigen::Index>(linearization->inputNames.size()),
      "Periodic input matrix dimensions do not match state and input names");

  const auto betaIndex = linearization->FindStateIndex("Beta");
  const auto yawRateIndex = linearization->FindStateIndex("R");
  const auto rudderIndex = linearization->FindInputIndex("DrCmd");
  Require(betaIndex && yawRateIndex && rudderIndex,
      "Periodic linearization is missing lateral states or rudder input");
  RequireNear(yawDynamics->aBetaBeta,
      linearization->A(*betaIndex, *betaIndex),
      SimTimeTolerance,
      "Yaw dynamics A(Beta, Beta) mismatch");
  RequireNear(yawDynamics->aBetaR,
      linearization->A(*betaIndex, *yawRateIndex),
      SimTimeTolerance,
      "Yaw dynamics A(Beta, R) mismatch");
  RequireNear(yawDynamics->aRBeta,
      linearization->A(*yawRateIndex, *betaIndex),
      SimTimeTolerance,
      "Yaw dynamics A(R, Beta) mismatch");
  RequireNear(yawDynamics->aRR,
      linearization->A(*yawRateIndex, *yawRateIndex),
      SimTimeTolerance,
      "Yaw dynamics A(R, R) mismatch");
  RequireNear(yawDynamics->bBetaRudder,
      linearization->B(*betaIndex, *rudderIndex),
      SimTimeTolerance,
      "Yaw dynamics B(Beta, DrCmd) mismatch");
  RequireNear(yawDynamics->bRRudder,
      linearization->B(*yawRateIndex, *rudderIndex),
      SimTimeTolerance,
      "Yaw dynamics B(R, DrCmd) mismatch");

  Require(simulation.Tick(), "Autopilot mode tick failed");

  const control::ControlInput &actualInput = aircraft.GetControls().GetInput();
  RequireNear(actualInput.elevator,
      0.2,
      SimTimeTolerance,
      "Roll Hold should pass through manual elevator");
  RequireNear(actualInput.aileron,
      -0.8,
      ControlCommandTolerance,
      "Unimplemented Roll Hold did not preserve manual aileron");
  RequireNear(actualInput.rudder,
      0.1,
      SimTimeTolerance,
      "Autopilot mode should pass through manual rudder");
  RequireNear(actualInput.throttle,
      0.4,
      SimTimeTolerance,
      "Autopilot mode should pass through manual throttle");
  const auto *rollHold = autopilot.GetController<gnc::RollHoldController>();
  Require(rollHold != nullptr && !rollHold->GetDiagnostics().controlOutputValid,
      "Unimplemented Roll Hold reported an active control output");
}

void TestUnimplementedRollHoldPassesThroughManualInput() {
  sim::Simulation simulation(std::make_unique<gnc::MyAutopilot>());
  StartSimulation(simulation);
  auto &aircraft = simulation.GetAircraft();
  auto &flightControlManager = GetFlightControlManager(simulation);
  auto &manualController = flightControlManager.GetManualController();
  auto &autopilot = GetMyAutopilot(simulation);

  manualController.SetCommandedInput({
      .elevator = 0.2,
      .aileron = -0.8,
      .rudder = 0.1,
      .throttle = 0.4,
  });

  const auto &properties = aircraft.GetProperties();
  const gnc::RollHoldSettings rollSettings{
      .targetRollRad = properties.Roll().Rad() + 0.1,
      .attitudeLoop = {.proportionalGain = 1.2},
      .rateLoop = {.proportionalGain = 0.4},
  };
  autopilot.SetRollHoldSettings(rollSettings);
  autopilot.SetRollHoldEnabled(true);
  flightControlManager.SetMode(control::FlightControlMode::Autopilot);

  Require(simulation.Tick(), "Roll hold pass-through tick failed");

  const control::ControlInput &actualInput = aircraft.GetControls().GetInput();
  RequireNear(actualInput.elevator,
      0.2,
      SimTimeTolerance,
      "Roll hold should pass through manual elevator");
  RequireNear(actualInput.aileron,
      -0.8,
      ControlCommandTolerance,
      "Unimplemented Roll Hold did not pass through manual aileron");
  RequireNear(actualInput.rudder,
      0.1,
      SimTimeTolerance,
      "Roll hold should pass through manual rudder");
  RequireNear(actualInput.throttle,
      0.4,
      SimTimeTolerance,
      "Roll hold should pass through manual throttle");
}

void TestFDMStateFlagOperations() {
  sim::FDMStateFlags flags = sim::FDMStateFlags::None;
  flags |= sim::FDMStateFlags::State;
  flags |= sim::FDMStateFlags::Controls;

  Require(sim::HasFDMStateFlag(flags, sim::FDMStateFlags::State),
      "State flag was not set");
  Require(sim::HasFDMStateFlag(flags, sim::FDMStateFlags::Controls),
      "Controls flag was not set");
  Require(!sim::HasFDMStateFlag(flags, sim::FDMStateFlags::Propulsion),
      "Propulsion flag was unexpectedly set");

  flags ^= sim::FDMStateFlags::Controls;
  Require(!sim::HasFDMStateFlag(flags, sim::FDMStateFlags::Controls),
      "XOR did not clear Controls flag");

  flags = sim::FDMStateFlags::All & ~sim::FDMStateFlags::Environment;
  Require(!sim::HasFDMStateFlag(flags, sim::FDMStateFlags::Environment),
      "Complement did not exclude Environment flag");
  Require(sim::HasFDMStateFlag(flags, sim::FDMStateFlags::Propulsion),
      "Complement removed an unrelated flag");
}

void TestFDMStateAndControlSynchronization() {
  sim::Aircraft source;
  sim::Aircraft target;

  sim::InitialCondition sourceCondition{};
  sourceCondition.latitudeDeg = 37.45;
  sourceCondition.longitudeDeg = 127.11;
  sourceCondition.altitudeFt = 3500.0;
  sourceCondition.rollDeg = 8.0;
  sourceCondition.pitchDeg = -3.0;
  sourceCondition.headingDeg = 42.0;
  sourceCondition.airspeedKts = 105.0;
  sourceCondition.pRadPerSec = 0.03;
  sourceCondition.qRadPerSec = -0.02;
  sourceCondition.rRadPerSec = 0.01;

  sim::InitialCondition targetCondition{};
  targetCondition.latitudeDeg = -12.0;
  targetCondition.longitudeDeg = 15.0;
  targetCondition.altitudeFt = 900.0;
  targetCondition.headingDeg = 210.0;
  targetCondition.airspeedKts = 70.0;

  Require(source.Initialize(MakeConfig(), sourceCondition),
      "Source Aircraft failed to initialize");
  Require(target.Initialize(MakeConfig(), targetCondition),
      "Target Aircraft failed to initialize");

  sim::FDMState sourceControlSetup =
      source.ExtractFDMState(sim::FDMStateFlags::Controls);
  sourceControlSetup.controls.elevatorCommand = 0.21;
  sourceControlSetup.controls.aileronCommand = -0.32;
  sourceControlSetup.controls.rudderCommand = 0.17;
  sourceControlSetup.controls.pitchTrimCommand = -0.08;
  std::fill(sourceControlSetup.controls.throttleCommands.begin(),
      sourceControlSetup.controls.throttleCommands.end(),
      0.64);
  sourceControlSetup.controls.elevatorPositionRad = 0.07;
  sourceControlSetup.controls.leftAileronPositionRad = -0.05;
  sourceControlSetup.controls.rightAileronPositionRad = 0.05;
  sourceControlSetup.controls.rudderPositionRad = 0.04;
  std::fill(sourceControlSetup.controls.throttlePositions.begin(),
      sourceControlSetup.controls.throttlePositions.end(),
      0.61);
  source.ApplyFDMState(sourceControlSetup);

  sim::FDMState targetControlSetup =
      target.ExtractFDMState(sim::FDMStateFlags::Controls);
  targetControlSetup.controls.elevatorCommand = -0.45;
  targetControlSetup.controls.aileronCommand = 0.36;
  targetControlSetup.controls.rudderCommand = -0.27;
  targetControlSetup.controls.pitchTrimCommand = 0.12;
  std::fill(targetControlSetup.controls.throttleCommands.begin(),
      targetControlSetup.controls.throttleCommands.end(),
      0.22);
  target.ApplyFDMState(targetControlSetup);

  const sim::FDMState targetControlsBeforeStateApply =
      target.ExtractFDMState(sim::FDMStateFlags::Controls);
  const double targetTimeBeforeStateApply =
      target.GetAircraftState().simulationTimeSec;
  const sim::FDMState sourceState =
      source.ExtractFDMState(sim::FDMStateFlags::State);

  Require(sourceState.flags == sim::FDMStateFlags::State,
      "Extracted state did not preserve requested flags");
  Require(sourceState.controls.throttleCommands.empty(),
      "State-only extraction populated Controls data");

  target.ApplyFDMState(sourceState);
  const sim::FDMState synchronizedState =
      target.ExtractFDMState(sim::FDMStateFlags::State);
  const sim::FDMState targetControlsAfterStateApply =
      target.ExtractFDMState(sim::FDMStateFlags::Controls);

  RequireKinematicStateNear(synchronizedState.state,
      sourceState.state,
      "State synchronization");
  RequireControlStateNear(targetControlsAfterStateApply.controls,
      targetControlsBeforeStateApply.controls,
      "State-only synchronization changed Controls");
  RequireNear(target.GetAircraftState().simulationTimeSec,
      targetTimeBeforeStateApply,
      SimTimeTolerance,
      "State synchronization advanced simulation time");

  const sim::FDMState targetStateBeforeControlApply =
      target.ExtractFDMState(sim::FDMStateFlags::State);
  const sim::FDMState sourceControls =
      source.ExtractFDMState(sim::FDMStateFlags::Controls);
  target.ApplyFDMState(sourceControls);
  const sim::FDMState synchronizedControls =
      target.ExtractFDMState(sim::FDMStateFlags::Controls);
  const sim::FDMState targetStateAfterControlApply =
      target.ExtractFDMState(sim::FDMStateFlags::State);

  RequireControlStateNear(synchronizedControls.controls,
      sourceControls.controls,
      "Control synchronization");
  RequireKinematicStateNear(targetStateAfterControlApply.state,
      targetStateBeforeControlApply.state,
      "Controls-only synchronization changed State");
  RequireNear(target.GetAircraftState().simulationTimeSec,
      targetTimeBeforeStateApply,
      SimTimeTolerance,
      "Control synchronization advanced simulation time");
}

void TestFDMPropulsionAndEnvironmentSynchronization() {
  sim::Aircraft source;
  sim::Aircraft target;
  Require(source.Initialize(MakeConfig(), {}),
      "Source Aircraft failed to initialize");
  Require(target.Initialize(MakeConfig(), {}),
      "Target Aircraft failed to initialize");

  constexpr sim::FDMStateFlags Flags =
      sim::FDMStateFlags::Propulsion | sim::FDMStateFlags::Environment;
  sim::FDMState sourceSetup = source.ExtractFDMState(Flags);
  Require(!sourceSetup.propulsion.engines.empty(),
      "Expected at least one engine for propulsion synchronization");

  sourceSetup.propulsion.engines[0].running = true;
  sourceSetup.propulsion.engines[0].engineRpm = 1350.0;
  sourceSetup.propulsion.engines[0].thrusterRpm = 1350.0;
  sourceSetup.environment.temperatureBiasRankine = 7.0;
  sourceSetup.environment.seaLevelGradedTemperatureDeltaRankine = 3.0;
  sourceSetup.environment.vaporMassFractionPpm = 2500.0;
  sourceSetup.environment.seaLevelPressurePsf = 2075.0;
  sourceSetup.environment.windNedFps = {12.0, -7.0, 2.0};
  sourceSetup.environment.gustNedFps = {1.5, -0.5, 0.25};
  sourceSetup.environment.turbulenceNedFps = {0.3, 0.2, -0.1};
  sourceSetup.environment.turbulenceGain = 0.4;
  sourceSetup.environment.turbulenceRate = 0.8;
  sourceSetup.environment.turbulenceRhythmicity = 0.6;
  sourceSetup.environment.windSpeedAt20FtFps = 9.0;
  sourceSetup.environment.terrainElevationFt = 350.0;

  source.ApplyFDMState(sourceSetup);
  const sim::FDMState sourceState = source.ExtractFDMState(Flags);
  const double targetTimeBeforeApply =
      target.GetAircraftState().simulationTimeSec;
  target.ApplyFDMState(sourceState);
  const sim::FDMState targetState = target.ExtractFDMState(Flags);

  Require(targetState.propulsion.engines.size()
              == sourceState.propulsion.engines.size(),
      "Synchronized engine count mismatch");
  for (std::size_t index = 0; index < sourceState.propulsion.engines.size();
      ++index) {
    const sim::FDMEngineState &actual = targetState.propulsion.engines[index];
    const sim::FDMEngineState &expected = sourceState.propulsion.engines[index];
    Require(actual.running == expected.running,
        "Synchronized engine running state mismatch");
    RequireNear(actual.engineRpm,
        expected.engineRpm,
        SimTimeTolerance,
        "Synchronized engine RPM mismatch");
    RequireNear(actual.thrusterRpm,
        expected.thrusterRpm,
        SimTimeTolerance,
        "Synchronized thruster RPM mismatch");
  }

  const sim::FDMEnvironmentState &actual = targetState.environment;
  const sim::FDMEnvironmentState &expected = sourceState.environment;
  RequireNear(actual.seaLevelTemperatureRankine,
      expected.seaLevelTemperatureRankine,
      SimTimeTolerance,
      "Synchronized sea-level temperature mismatch");
  RequireNear(actual.seaLevelPressurePsf,
      expected.seaLevelPressurePsf,
      SimTimeTolerance,
      "Synchronized sea-level pressure mismatch");
  Require(actual.hasStandardAtmosphere == expected.hasStandardAtmosphere,
      "Synchronized atmosphere model mismatch");
  RequireNear(actual.temperatureBiasRankine,
      expected.temperatureBiasRankine,
      SimTimeTolerance,
      "Synchronized temperature bias mismatch");
  RequireNear(actual.seaLevelGradedTemperatureDeltaRankine,
      expected.seaLevelGradedTemperatureDeltaRankine,
      SimTimeTolerance,
      "Synchronized graded temperature delta mismatch");
  RequireNear(actual.vaporMassFractionPpm,
      expected.vaporMassFractionPpm,
      SimTimeTolerance,
      "Synchronized vapor fraction mismatch");
  RequireArrayNear(actual.windNedFps,
      expected.windNedFps,
      SimTimeTolerance,
      "Synchronized wind mismatch");
  RequireArrayNear(actual.gustNedFps,
      expected.gustNedFps,
      SimTimeTolerance,
      "Synchronized gust mismatch");
  RequireArrayNear(actual.turbulenceNedFps,
      expected.turbulenceNedFps,
      SimTimeTolerance,
      "Synchronized turbulence mismatch");
  Require(actual.turbulenceType == expected.turbulenceType,
      "Synchronized turbulence type mismatch");
  RequireNear(actual.turbulenceGain,
      expected.turbulenceGain,
      SimTimeTolerance,
      "Synchronized turbulence gain mismatch");
  RequireNear(actual.turbulenceRate,
      expected.turbulenceRate,
      SimTimeTolerance,
      "Synchronized turbulence rate mismatch");
  RequireNear(actual.turbulenceRhythmicity,
      expected.turbulenceRhythmicity,
      SimTimeTolerance,
      "Synchronized turbulence rhythmicity mismatch");
  RequireNear(actual.windSpeedAt20FtFps,
      expected.windSpeedAt20FtFps,
      SimTimeTolerance,
      "Synchronized 20-foot wind speed mismatch");
  RequireNear(actual.terrainElevationFt,
      expected.terrainElevationFt,
      1.0e-6,
      "Synchronized terrain elevation mismatch");
  Require(actual.gravityType == expected.gravityType,
      "Synchronized gravity type mismatch");
  RequireNear(actual.planetRotationRateRadPerSec,
      expected.planetRotationRateRadPerSec,
      SimTimeTolerance,
      "Synchronized planet rotation rate mismatch");
  RequireNear(target.GetAircraftState().simulationTimeSec,
      targetTimeBeforeApply,
      SimTimeTolerance,
      "Propulsion/environment synchronization advanced simulation time");
}

} // namespace

int main() {
  try {
    TestErrorTrackerOwnsErrorState();
    TestSimulationComponentLifecycle();
    TestTickAdvancesOneStep();
    TestStepUsesRequestedDeltaTime();
    TestSimulationInstancesAreIsolatedAndDeterministic();
    TestSimulationPublishesAircraftTelemetry();
    TestResetUsesDefaultInitialCondition();
    TestResetWithInitialCondition();
    TestCaptureCurrentStateCanReset();
    TestEngineStateInspection();
    TestInvalidInitialConditionFails();
    TestAircraftStateAccess();
    TestNavigationProperties();
    TestStartAppliesInitialTrim();
    TestInitialTrimIsStoredInSimulation();
    TestSimulationUsesInjectedAutopilot();
    TestPx4AutopilotOwnsBaselineRollReference();
    TestAutopilotControllerRegistry();
    TestControlSystemAxisSettersClampFinalInput();
    TestControlSystemSetInputClampsFinalInput();
    TestManualFlightControlControllerAppliesCommands();
    TestFlightControlManagerOwnsAndRoutesControllers();
    TestFlightControlManagerNoInputPreservesCommand();
    TestManualModeIgnoresAutopilotSource();
    TestAutomaticLinearizationToggle();
    TestLinearizationRunsInManualModeWithoutHolds();
    TestRollHoldControllerHasNoControlLaw();
    TestUnimplementedPrimaryRollHoldPublishesNoControlTelemetry();
    TestPx4RollHoldReferenceComputesBaselineCommand();
    TestPx4BaselineRollHoldControlsAircraft();
    TestAutopilotModeWithUnimplementedRollHoldPassesThrough();
    TestUnimplementedRollHoldPassesThroughManualInput();
    TestFDMStateFlagOperations();
    TestFDMStateAndControlSynchronization();
    TestFDMPropulsionAndEnvironmentSynchronization();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }

  return 0;
}
