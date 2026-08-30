#pragma once

#include "gui/architecture/EventSink.hpp"

#include <concepts>
#include <utility>

namespace gui::architecture {
// Optional storage shared by lightweight feature controllers. Derived
// controllers add explicit dependencies and are the only new-code layer that
// should edit the model or translate child events.
template <typename Model, typename ParentEvent> class FeatureController {
public:
  const Model &GetModel() const { return model_; }

protected:
  explicit FeatureController(Model model = {},
      EventSink<ParentEvent> parentEvents = {})
      : model_(std::move(model)), parentEvents_(std::move(parentEvents)) {}

  Model &EditModel() { return model_; }

  void EmitToParent(const ParentEvent &event) const {
    parentEvents_.Emit(event);
  }

private:
  Model model_;
  EventSink<ParentEvent> parentEvents_;
};

// Feature controllers render from immutable props supplied by their owner.
template <typename Controller, typename Props>
concept FeatureFor = requires(Controller &controller, const Props &props) {
  { controller.Render(props) } -> std::same_as<void>;
};
} // namespace gui::architecture
