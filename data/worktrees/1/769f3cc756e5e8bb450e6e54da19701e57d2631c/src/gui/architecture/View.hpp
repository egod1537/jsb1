#pragma once

#include "gui/architecture/EventSink.hpp"

#include <concepts>

namespace gui::architecture {
// Preferred view boundary: immutable model/props in, typed interactions out.
// A matching view cannot receive GUI as a general-purpose service locator.
template <typename View, typename Model, typename Props, typename Event>
concept ViewFor = requires(const View &view, const Model &model,
    const Props &props, EventSink<Event> events) {
  { view.Render(model, props, events) } -> std::same_as<void>;
};
} // namespace gui::architecture
