#include "gui/features/gnc/GNCController.hpp"
#include "gui/features/linearization/LinearizationController.hpp"
#include "gui/features/simulation/ScenarioController.hpp"
#include "gui/features/simulation/SimulationController.hpp"
#include "messaging/MessageBus.hpp"
#include "messaging/SimulationMessageClient.hpp"
#include "messaging/SimulationMessages.hpp"

#include <array>
#include <cassert>
#include <string_view>

namespace {
namespace messaging = application::messaging;

void TestSimulationEventsPublishCommandsAndUpdateChildModel() {
  messaging::MessageBus bus;
  application::SimulationMessageClient client(bus);
  gui::SimulationController controller(client);

  int startCount = 0;
  double requestedHz = 0.0;
  bool maximumSpeed = false;
  auto startSubscription = bus.Subscribe<messaging::SimulationStartCommand>(
      [&startCount](const auto &) { ++startCount; });
  auto rateSubscription = bus.Subscribe<messaging::SimulationRateCommand>(
      [&requestedHz](const auto &command) { requestedHz = command.hz; });
  auto maximumSubscription =
      bus.Subscribe<messaging::SimulationMaximumSpeedCommand>(
          [&maximumSpeed](
              const auto &command) { maximumSpeed = command.enabled; });

  controller.Handle(gui::SimulationStartRequested{});
  controller.Handle(gui::SimulationRateChanged{120.0});
  controller.Handle(gui::MaximumSimulationSpeedChanged{true});
  assert(startCount == 1);
  assert(requestedHz == 120.0);
  assert(maximumSpeed);

  sim::SimulationSnapshot snapshot;
  snapshot.defaultInitialCondition.altitudeFt = 4000.0;
  controller.Synchronize(snapshot);
  controller.Handle({gui::InitialConditionField::AltitudeFt, 5500.0});
  assert(controller.GetInitialConditionModel().pending.altitudeFt == 5500.0);
}

void TestPlaybackToggleSelectsStartOrStopFromRuntimeState() {
  messaging::MessageBus bus;
  application::SimulationMessageClient client(bus);
  gui::SimulationController controller(client);
  int startCount = 0;
  int stopCount = 0;
  auto startSubscription = bus.Subscribe<messaging::SimulationStartCommand>(
      [&startCount](const auto &) { ++startCount; });
  auto stopSubscription = bus.Subscribe<messaging::SimulationStopCommand>(
      [&stopCount](const auto &) { ++stopCount; });

  controller.Handle(gui::SimulationPlaybackToggled{});
  assert(startCount == 1);
  assert(stopCount == 0);

  sim::SimulationStatus status;
  status.executionState = sim::SimulationExecutionState::Running;
  bus.Publish(messaging::SimulationStatusEvent{.status = status});
  controller.Handle(gui::SimulationPlaybackToggled{});
  assert(startCount == 1);
  assert(stopCount == 1);

  status.executionState = sim::SimulationExecutionState::Paused;
  bus.Publish(messaging::SimulationStatusEvent{.status = status});
  controller.Handle(gui::SimulationPlaybackToggled{});
  assert(stopCount == 2);
}

void TestGNCEventsUpdateModelAndPublishCompleteConfig() {
  messaging::MessageBus bus;
  application::SimulationMessageClient client(bus);
  gui::GNCController controller(client);
  sim::SimulationSnapshot snapshot;
  snapshot.primary.available = true;
  snapshot.primaryAutopilot.available = true;
  controller.Synchronize(snapshot);

  controller.Handle(
      gui::PrimaryRollHoldValueChanged{gui::PrimaryRollHoldField::TargetDeg,
          12.5});
  assert(controller.GetModel().primaryAutopilot.rollTargetDeg == 12.5);

  sim::PrimaryRollHoldConfig published;
  auto subscription = bus.Subscribe<messaging::PrimaryRollHoldConfigCommand>(
      [&published](const auto &command) { published = command.config; });
  controller.PublishConfiguration(snapshot);
  assert(published.targetRollRad != 0.0);
  assert(published.rollAngleProportionalGain
         == controller.GetModel().primaryAutopilot.rollAngleProportionalGain);
}

void TestBaselinePx4TuningUsesOfficialMetadata() {
  struct ExpectedParameter {
    std::string_view name;
    double minimum;
    double maximum;
    double defaultValue;
    double increment;
  };
  constexpr std::array<ExpectedParameter, 7> ExpectedParameters{{
      {"FW_R_TC", 0.2, 1.0, 0.4, 0.05},
      {"FW_R_RMAX", 0.0, 180.0, 70.0, 0.5},
      {"FW_RR_P", 0.0, 10.0, 0.05, 0.005},
      {"FW_RR_I", 0.0, 10.0, 0.1, 0.01},
      {"FW_RR_D", 0.0, 10.0, 0.0, 0.005},
      {"FW_RR_FF", 0.0, 10.0, 0.5, 0.05},
      {"FW_RR_IMAX", 0.0, 1.0, 0.2, 0.05},
  }};

  assert(gnc::Px4RollHoldParameters.size() == ExpectedParameters.size());
  for (std::size_t index = 0; index < ExpectedParameters.size(); ++index) {
    const auto &metadata = gnc::Px4RollHoldParameters[index];
    const auto &expected = ExpectedParameters[index];
    assert(metadata.name == expected.name);
    assert(metadata.minimum == expected.minimum);
    assert(metadata.maximum == expected.maximum);
    assert(metadata.defaultValue == expected.defaultValue);
    assert(metadata.increment == expected.increment);
  }

  messaging::MessageBus bus;
  application::SimulationMessageClient client(bus);
  gui::GNCController controller(client);

  assert(gui::BaselinePx4RollHoldParameterBindings.size() == 7);
  for (const auto &binding : gui::BaselinePx4RollHoldParameterBindings) {
    const auto &metadata =
        gnc::GetPx4RollHoldParameterMetadata(binding.parameter);

    controller.Handle(gui::BaselineRollHoldValueChanged{binding.field,
        metadata.minimum - 1000.0});
    assert(controller.GetModel().baselineAutopilot.*(binding.value)
           == metadata.minimum);

    controller.Handle(gui::BaselineRollHoldValueChanged{binding.field,
        metadata.maximum + 1000.0});
    assert(controller.GetModel().baselineAutopilot.*(binding.value)
           == metadata.maximum);
  }

  controller.Handle(gui::BaselineRollHoldValueChanged{
      gui::BaselineRollHoldField::RateProportionalGain,
      0.1234});
  assert(controller.GetModel().baselineAutopilot.px4RollRateProportionalGain
         == 0.123);

  controller.Handle(gui::BaselineRollHoldTuningResetRequested{});
  for (const auto &binding : gui::BaselinePx4RollHoldParameterBindings) {
    const auto &metadata =
        gnc::GetPx4RollHoldParameterMetadata(binding.parameter);
    assert(controller.GetModel().baselineAutopilot.*(binding.value)
           == metadata.defaultValue);
  }
}

void TestLinearizationEventPublishesCommand() {
  messaging::MessageBus bus;
  application::SimulationMessageClient client(bus);
  gui::LinearizationController controller(client);
  bool automatic = false;
  auto subscription = bus.Subscribe<messaging::LinearizationConfigCommand>(
      [&automatic](const auto &command) {
        automatic = command.automaticUpdatesEnabled;
      });

  controller.Handle(gui::AutomaticLinearizationChanged{true});
  controller.Handle(gui::LinearizationValueTransformChanged{
      gui::LinearizationValueTransform::SignedLog10});

  assert(automatic);
  assert(controller.GetModel().valueTransform
         == gui::LinearizationValueTransform::SignedLog10);
}

void TestScenarioChildUpdatesDraftAndEmitsLaunchIntent() {
  bool launchReceived = false;
  gui::ScenarioController *controllerPtr = nullptr;
  gui::ScenarioController controller({},
      gui::architecture::EventSink<gui::ScenarioLaunchRequested>{
          [&launchReceived, &controllerPtr](
              const gui::ScenarioLaunchRequested &event) {
            launchReceived =
                event.request.scenario.events.front().command.rollDeg == 14.0;
            controllerPtr->Handle(gui::ScenarioApplyCompleted{
                .succeeded = true,
            });
          }});
  controllerPtr = &controller;
  sim::SimulationScenario draft = controller.GetModel().draft;
  draft.events.front().command.rollDeg = 14.0;

  controller.Handle(gui::ScenarioDraftChanged{draft});
  assert(controller.Apply());

  assert(controller.GetModel().draft.events.front().command.rollDeg == 14.0);
  assert(launchReceived);
}
} // namespace

int main() {
  TestSimulationEventsPublishCommandsAndUpdateChildModel();
  TestPlaybackToggleSelectsStartOrStopFromRuntimeState();
  TestGNCEventsUpdateModelAndPublishCompleteConfig();
  TestBaselinePx4TuningUsesOfficialMetadata();
  TestLinearizationEventPublishesCommand();
  TestScenarioChildUpdatesDraftAndEmitsLaunchIntent();
  return 0;
}
