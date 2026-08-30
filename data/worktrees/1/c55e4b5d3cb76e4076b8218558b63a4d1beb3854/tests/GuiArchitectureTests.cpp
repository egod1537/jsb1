#include "gui/architecture/Feature.hpp"
#include "gui/architecture/View.hpp"

#include <cassert>
#include <type_traits>
#include <utility>

namespace {
namespace architecture = gui::architecture;

struct ToggleValueChanged {
  bool value = false;
};

struct ChildEnabledChanged {
  bool enabled = false;
};

struct ParentEvent {};

struct ChildModel {
  bool enabled = false;
};

struct ChildProps {
  bool editable = true;
};

class DumbToggleView {
public:
  void Render(const ChildModel &, const ChildProps &,
      architecture::EventSink<ToggleValueChanged>) const {}
};

static_assert(architecture::ViewFor<DumbToggleView, ChildModel, ChildProps,
    ToggleValueChanged>);
static_assert(
    !std::is_constructible_v<architecture::EventSink<ToggleValueChanged>,
        architecture::EventHandler<ChildEnabledChanged>>);

class ChildController final
    : public architecture::FeatureController<ChildModel, ChildEnabledChanged> {
public:
  explicit ChildController(
      architecture::EventSink<ChildEnabledChanged> parentEvents = {})
      : FeatureController({}, std::move(parentEvents)) {}

  void Render(const ChildProps &props) {
    view_.Render(GetModel(),
        props,
        architecture::EventSink<ToggleValueChanged>{
            [this, props](const ToggleValueChanged &event) {
              HandleEvent(event, props);
            }});
  }

  void HandleEvent(const ToggleValueChanged &event, const ChildProps &props) {
    if (!props.editable || GetModel().enabled == event.value) {
      return;
    }
    EditModel().enabled = event.value;
    EmitToParent({event.value});
  }

private:
  DumbToggleView view_;
};

static_assert(architecture::FeatureFor<ChildController, ChildProps>);

struct ParentModel {
  bool childEnabled = false;
};

class FakeSettingsService {
public:
  void SetEnabled(bool enabled) {
    value = enabled;
    ++commandCount;
  }

  bool value = false;
  int commandCount = 0;
};

class ParentController final
    : public architecture::FeatureController<ParentModel, ParentEvent> {
public:
  explicit ParentController(FakeSettingsService &settings)
      : settings_(settings),
        child_(architecture::EventSink<ChildEnabledChanged>{
            [this](const ChildEnabledChanged &event) {
              HandleChildEvent(event);
            }}) {}

  ChildController &GetChild() { return child_; }

private:
  void HandleChildEvent(const ChildEnabledChanged &event) {
    EditModel().childEnabled = event.enabled;
    settings_.SetEnabled(event.enabled);
  }

  FakeSettingsService &settings_;
  ChildController child_;
};

void TestTypedEventSink() {
  bool received = false;
  const architecture::EventSink<ToggleValueChanged> events{
      [&received](const ToggleValueChanged &event) { received = event.value; }};
  assert(events.IsConnected());
  events.Emit({true});
  assert(received);

  architecture::EventSink<ToggleValueChanged> disconnected;
  assert(!disconnected.IsConnected());
  disconnected.Emit({true});
}

void TestChildEventUpdatesControllerAndPropagatesSemantically() {
  FakeSettingsService settings;
  ParentController parent(settings);

  parent.GetChild().HandleEvent({true}, {.editable = true});

  assert(parent.GetChild().GetModel().enabled);
  assert(parent.GetModel().childEnabled);
  assert(settings.value);
  assert(settings.commandCount == 1);
}

void TestControllerRejectsDisabledInteraction() {
  FakeSettingsService settings;
  ParentController parent(settings);

  parent.GetChild().HandleEvent({true}, {.editable = false});

  assert(!parent.GetChild().GetModel().enabled);
  assert(!parent.GetModel().childEnabled);
  assert(settings.commandCount == 0);
}
} // namespace

int main() {
  TestTypedEventSink();
  TestChildEventUpdatesControllerAndPropagatesSemantically();
  TestControllerRejectsDisabledInteraction();
  return 0;
}
