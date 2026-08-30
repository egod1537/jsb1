#include "messaging/MessageBus.hpp"
#include "messaging/SimulationMessageAdapter.hpp"
#include "messaging/SimulationMessageClient.hpp"
#include "sim/Simulation.hpp"
#include "sim/gnc/autopilot/AutopilotFactory.hpp"
#include "sim/runtime/SimulationRuntime.hpp"
#include "sim/scenario/SimulationScenario.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
struct Harness {
  Harness()
      : runtime(std::make_unique<sim::Simulation>(
                    gnc::CreateAutopilot(gnc::AutopilotKind::Primary)),
            std::make_unique<sim::Simulation>(
                gnc::CreateAutopilot(gnc::AutopilotKind::Baseline))),
        adapter(bus, runtime), client(bus) {}

  bool Initialize() {
    const bool initialized = runtime.Initialize(sim::SimulationConfig{});
    adapter.PublishState();
    return initialized;
  }

  bool Tick() {
    const bool succeeded = runtime.Tick();
    adapter.PublishState();
    return succeeded;
  }

  application::messaging::MessageBus bus;
  sim::SimulationRuntime runtime;
  application::messaging::SimulationMessageAdapter adapter;
  application::SimulationMessageClient client;
};

void TestCommandsSnapshotsAndTelemetry() {
  Harness harness;
  assert(harness.Initialize());
  assert(harness.client.GetSimulationSnapshot().baseline.has_value());

  harness.client.StartSimulation();
  assert(harness.client.GetSimulationExecutionState()
         == sim::SimulationExecutionState::Running);
  assert(harness.Tick());
  const double runningTime =
      harness.client.GetSimulationSnapshot().primary.aircraft.simulationTimeSec;
  assert(runningTime > 0.0);

  harness.client.PauseSimulation();
  assert(harness.client.GetSimulationExecutionState()
         == sim::SimulationExecutionState::Paused);
  harness.client.RequestSimulationTick();
  assert(harness.client.GetPendingSimulationTickCount() == 1);
  assert(harness.Tick());
  assert(harness.client.GetPendingSimulationTickCount() == 0);
  assert(
      harness.client.GetSimulationSnapshot().primary.aircraft.simulationTimeSec
      > runningTime);

  control::ControlInput input;
  input.aileron = 0.25;
  input.throttle = 0.7;
  assert(harness.client.SetManualControl(input));
  assert(!harness.client.GetLastCommandError().has_value());
  assert(std::abs(harness.client.GetSimulationSnapshot()
                      .primaryAutopilot.manualControl.aileron
                  - input.aileron)
         < 1.0e-12);

  assert(harness.client.ResetSimulation());
  const auto primaryTelemetry =
      harness.client.GetTelemetrySnapshot(sim::SimulationSlot::Primary);
  const auto baselineTelemetry =
      harness.client.GetTelemetrySnapshot(sim::SimulationSlot::Baseline);
  assert(primaryTelemetry != nullptr && primaryTelemetry->available);
  assert(baselineTelemetry != nullptr && baselineTelemetry->available);

  harness.client.ResumeSimulation();
  assert(harness.client.GetSimulationExecutionState()
         == sim::SimulationExecutionState::Running);
  harness.client.StopSimulation();
  assert(harness.client.GetSimulationExecutionState()
         == sim::SimulationExecutionState::Stopped);
}

void TestFailedRequestPreservesErrorAndSuccessClearsIt() {
  application::messaging::MessageBus bus;
  sim::SimulationRuntime runtime(std::make_unique<sim::Simulation>(
      gnc::CreateAutopilot(gnc::AutopilotKind::Primary)));
  application::messaging::SimulationMessageAdapter adapter(bus, runtime);
  application::SimulationMessageClient client(bus);

  assert(!client.ResetSimulation());
  const std::optional<std::string> failure = client.GetLastCommandError();
  assert(failure.has_value());
  assert(*failure == "Simulation reset failed.");

  assert(runtime.Initialize(sim::SimulationConfig{}));
  adapter.PublishState();
  assert(client.ResetSimulation());
  assert(!client.GetLastCommandError().has_value());
}

void TestMissingSynchronousResultFailsCleanly() {
  application::messaging::MessageBus bus;
  application::SimulationMessageClient client(bus);

  assert(!client.SetManualControl(control::ControlInput{}));
  const std::optional<std::string> failure = client.GetLastCommandError();
  assert(failure.has_value());
  assert(*failure
         == "Synchronous message request completed without a matching result.");
}

void TestTrimAndScenarioRequestResults() {
  Harness harness;
  assert(harness.Initialize());

  gnc::TrimRequest trimRequest;
  assert(harness.client.RunTrim(trimRequest, false));
  const sim::SimulationSnapshot trimmed =
      harness.client.GetSimulationSnapshot();
  assert(trimmed.trim.result.has_value());
  assert(trimmed.trim.result->success);

  sim::SimulationScenario scenario;
  scenario.name = "Message bus integration";
  scenario.runTrim = false;
  scenario.durationSec = 0.2;
  scenario.events.front().timeSec = 0.1;
  assert(harness.client.RunExecution({
      .scenario = scenario,
      .variant = sim::ExecutionVariant::Primary,
  }));
  assert(harness.client.GetScenarioExecutionStatus().has_value());
  while (harness.client.GetSimulationExecutionState()
         == sim::SimulationExecutionState::Running) {
    assert(harness.Tick());
  }

  const sim::SimulationSnapshot completed =
      harness.client.GetSimulationSnapshot();
  assert(completed.status.executionState
         == sim::SimulationExecutionState::Stopped);
  assert(!completed.status.scenario.has_value());
  assert(completed.appliedExecution.has_value());
  assert(completed.appliedExecution->scenario == scenario);
  assert(completed.appliedExecution->variant == sim::ExecutionVariant::Primary);
  assert(completed.primary.available);
  assert(completed.baseline.has_value() && completed.baseline->available);
  assert(harness.client.GetTelemetrySnapshot(sim::SimulationSlot::Primary)
          ->available);
  assert(harness.client.GetTelemetrySnapshot(sim::SimulationSlot::Baseline)
          ->available);
}

void TestTelemetryCacheRetainsFullSessionRangeEfficiently() {
  application::messaging::MessageBus bus;
  application::SimulationMessageClient client(bus);
  constexpr std::size_t SampleCount = 10'000;
  for (std::size_t index = 0; index < SampleCount; ++index) {
    bus.Publish(application::messaging::TelemetryFrameEvent{
        .slot = sim::SimulationSlot::Primary,
        .frame =
            {
                .available = true,
                .sequence = index + 1,
                .timestamp = static_cast<double>(index),
                .values = {{
                    .path = "test/long_history",
                    .value = index % 2 == 0 ? -1000.0 : 1000.0,
                }},
            },
    });
  }

  const auto snapshot =
      client.GetTelemetrySnapshot(sim::SimulationSlot::Primary);
  assert(snapshot != nullptr && snapshot->publishedTimeRange.has_value());
  assert(snapshot->publishedTimeRange->minSec == 0.0);
  assert(snapshot->publishedTimeRange->maxSec
         == static_cast<double>(SampleCount - 1));
  const telemetry::TelemetrySeries *series =
      snapshot->Find("test/long_history");
  assert(series != nullptr && !series->samples.empty());
  assert(series->samples.size() <= 4096);
  assert(series->samples.front().timeSec == 0.0);
  assert(
      series->samples.back().timeSec == static_cast<double>(SampleCount - 1));
  for (std::size_t index = 1; index < series->samples.size(); ++index) {
    assert(series->samples[index - 1].timeSec < series->samples[index].timeSec);
  }
  const bool retainedMinimum = std::any_of(series->samples.begin(),
      series->samples.end(),
      [](const telemetry::TelemetrySample &sample) {
        return sample.value == -1000.0;
      });
  const bool retainedMaximum = std::any_of(series->samples.begin(),
      series->samples.end(),
      [](const telemetry::TelemetrySample &sample) {
        return sample.value == 1000.0;
      });
  assert(retainedMinimum && retainedMaximum);
}
} // namespace

int main() {
  TestCommandsSnapshotsAndTelemetry();
  TestFailedRequestPreservesErrorAndSuccessClearsIt();
  TestMissingSynchronousResultFailsCleanly();
  TestTrimAndScenarioRequestResults();
  TestTelemetryCacheRetainsFullSessionRangeEfficiently();
  return 0;
}
