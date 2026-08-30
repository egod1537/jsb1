#include "messaging/MessageBus.hpp"
#include "messaging/SimulationMessageAdapter.hpp"
#include "messaging/SimulationMessageClient.hpp"
#include "sim/Simulation.hpp"
#include "sim/gnc/autopilot/MyAutopilot.hpp"
#include "sim/gnc/autopilot/PX4Autopilot.hpp"
#include "sim/runtime/SimulationRuntime.hpp"
#include "sim/execution/ExecutionVariantResolver.hpp"
#include "sim/scenario/SimulationScenario.hpp"
#include "common/math/Math.hpp"

#include <cassert>
#include <cmath>
#include <memory>

namespace {
bool NearlyEqual(double left, double right, double tolerance = 1.0e-6) {
  return std::abs(left - right) <= tolerance;
}

std::unique_ptr<sim::Simulation> MakePrimarySimulation() {
  return std::make_unique<sim::Simulation>(
      std::make_unique<gnc::MyAutopilot>());
}

std::unique_ptr<sim::Simulation> MakeBaselineSimulation() {
  return std::make_unique<sim::Simulation>(
      std::make_unique<gnc::PX4Autopilot>());
}

sim::ExecutionRequest MakeRequest(const sim::SimulationScenario &scenario,
    sim::ExecutionVariant variant = sim::ExecutionVariant::Primary) {
  return {.scenario = scenario, .variant = variant};
}

sim::ResolvedExecutionSpec Resolve(const sim::SimulationScenario &scenario,
    sim::ExecutionVariant variant) {
  sim::ResolvedExecutionSpec resolved;
  std::string error;
  assert(sim::ExecutionVariantResolver::Resolve(MakeRequest(scenario, variant),
      resolved,
      error));
  return resolved;
}

void TestInteractiveRuntimeExecution() {
  application::messaging::MessageBus bus;
  sim::SimulationRuntime runtime(MakePrimarySimulation());
  application::messaging::SimulationMessageAdapter adapter(bus, runtime);
  application::SimulationMessageClient control(bus);

  assert(control.GetSimulationExecutionState()
         == application::SimulationExecutionState::Stopped);
  assert(!control.GetScenarioExecutionStatus().has_value());
  assert(!control.GetSimulationSnapshot().appliedExecution.has_value());
  assert(!control.RunExecution(MakeRequest(sim::SimulationScenario{})));
  assert(runtime.Initialize(sim::SimulationConfig{}));
  adapter.PublishState();

  control.StartSimulation();
  assert(control.GetSimulationExecutionState()
         == application::SimulationExecutionState::Running);
  assert(runtime.Tick());
  adapter.PublishState();
  const double runningTime =
      control.GetSimulationSnapshot().primary.aircraft.simulationTimeSec;
  assert(runningTime > 0.0);

  control.PauseSimulation();
  control.RequestSimulationTick();
  assert(control.GetPendingSimulationTickCount() == 1);
  assert(runtime.Tick());
  adapter.PublishState();
  assert(control.GetPendingSimulationTickCount() == 0);
  assert(control.GetSimulationSnapshot().primary.aircraft.simulationTimeSec
         > runningTime);

  assert(control.ResetSimulation());
  assert(control.GetSimulationExecutionState()
         == application::SimulationExecutionState::Paused);
  control.ResumeSimulation();
  control.StopSimulation();
  assert(control.GetSimulationExecutionState()
         == application::SimulationExecutionState::Stopped);
}

void TestScenarioExecutesOnlyScenarioSelectedAutopilot() {
  application::messaging::MessageBus bus;
  sim::SimulationRuntime runtime(MakePrimarySimulation(),
      MakeBaselineSimulation());
  assert(runtime.Initialize(sim::SimulationConfig{}));
  application::messaging::SimulationMessageAdapter adapter(bus, runtime);
  application::SimulationMessageClient control(bus);
  adapter.PublishState();

  sim::SimulationScenario scenario;
  scenario.name = "Dual Roll Hold";
  scenario.initialCondition.altitudeFt = 4200.0;
  scenario.initialCondition.airspeedKts = 105.0;
  scenario.initialCondition.rollDeg = 2.0;
  scenario.initialCondition.pitchDeg = 1.0;
  scenario.initialCondition.headingDeg = 35.0;
  scenario.runTrim = false;
  scenario.events.front().timeSec = 0.0;
  scenario.events.front().command.rollDeg = 8.0;
  scenario.durationSec = 12.0;

  sim::SimulationScenario invalidScenario = scenario;
  invalidScenario.settlingBandDeg = -1.0;
  assert(!control.RunExecution(MakeRequest(invalidScenario)));
  assert(control.GetSimulationExecutionState()
         == application::SimulationExecutionState::Stopped);
  assert(!control.GetScenarioExecutionStatus().has_value());

  assert(control.RunExecution(MakeRequest(scenario)));
  assert(control.GetSimulationExecutionState()
         == application::SimulationExecutionState::Running);
  const auto status = control.GetScenarioExecutionStatus();
  assert(status.has_value());
  assert(status->name == scenario.name);
  assert(status->elapsedSec == 0.0);
  assert(status->durationSec == scenario.durationSec);

  const sim::SimulationSnapshot snapshot = control.GetSimulationSnapshot();
  assert(snapshot.appliedExecution.has_value());
  assert(snapshot.appliedExecution->scenario == scenario);
  assert(snapshot.appliedExecution->variant == sim::ExecutionVariant::Primary);
  assert(snapshot.baseline.has_value());
  assert(snapshot.baselineAutopilot.has_value());
  const sim::InitialCondition &primaryCondition =
      snapshot.primary.currentCondition;
  assert(NearlyEqual(primaryCondition.altitudeFt,
      scenario.initialCondition.altitudeFt));
  assert(NearlyEqual(primaryCondition.airspeedKts,
      scenario.initialCondition.airspeedKts));
  assert(
      NearlyEqual(primaryCondition.rollDeg, scenario.initialCondition.rollDeg));
  assert(NearlyEqual(primaryCondition.pitchDeg,
      scenario.initialCondition.pitchDeg));
  assert(NearlyEqual(primaryCondition.headingDeg,
      scenario.initialCondition.headingDeg));
  assert(snapshot.primaryAutopilot.primaryRollHold.enabled);
  assert(!snapshot.baselineAutopilot->baselineRollHold.enabled);
  assert(std::abs(snapshot.primaryAutopilot.primaryRollHold.targetRollRad
                  - math::DegToRad(scenario.events.front().command.rollDeg))
         < 1.0e-12);
  const auto primaryTelemetry =
      control.GetTelemetrySnapshot(sim::SimulationSlot::Primary);
  const auto baselineTelemetry =
      control.GetTelemetrySnapshot(sim::SimulationSlot::Baseline);
  assert(primaryTelemetry != nullptr && primaryTelemetry->available);
  assert(baselineTelemetry != nullptr && baselineTelemetry->available);
  assert(primaryTelemetry != baselineTelemetry);

  control.StopSimulation();
  const sim::SimulationSnapshot stopped = control.GetSimulationSnapshot();
  assert(
      stopped.status.executionState == sim::SimulationExecutionState::Stopped);
  assert(!stopped.status.scenario.has_value());
  assert(stopped.appliedExecution.has_value());
  assert(stopped.appliedExecution->scenario == scenario);
  assert(!stopped.primaryAutopilot.primaryRollHold.enabled);
  assert(stopped.baselineAutopilot.has_value());
  assert(!stopped.baselineAutopilot->baselineRollHold.enabled);

  assert(!control.RunExecution(MakeRequest(invalidScenario)));
  const sim::SimulationSnapshot afterRejectedApply =
      control.GetSimulationSnapshot();
  assert(afterRejectedApply.appliedExecution.has_value());
  assert(afterRejectedApply.appliedExecution->scenario == scenario);
}

void TestBaselineScenarioSelectsBaselineWithoutRebuild() {
  sim::SimulationRuntime runtime(MakePrimarySimulation(),
      MakeBaselineSimulation());
  assert(runtime.Initialize(sim::SimulationConfig{}));
  sim::SimulationScenario scenario;
  scenario.runTrim = false;
  scenario.events.front().timeSec = 0.0;
  scenario.durationSec = 0.1;
  assert(
      runtime.RunExecution(Resolve(scenario, sim::ExecutionVariant::Baseline)));
  const sim::SimulationSnapshot running = runtime.GetSnapshot();
  assert(running.primaryAutopilot.strategyName == "PX4Autopilot");
  assert(running.primaryAutopilot.baselineRollHold.enabled);
  assert(running.baselineAutopilot.has_value());
  assert(running.baselineAutopilot->strategyName == "MyAutopilot");
  runtime.Stop();
  const sim::SimulationSnapshot restored = runtime.GetSnapshot();
  assert(restored.primaryAutopilot.strategyName == "MyAutopilot");
  assert(restored.baselineAutopilot->strategyName == "PX4Autopilot");
}
} // namespace

int main() {
  TestInteractiveRuntimeExecution();
  TestScenarioExecutesOnlyScenarioSelectedAutopilot();
  TestBaselineScenarioSelectsBaselineWithoutRebuild();
  return 0;
}
