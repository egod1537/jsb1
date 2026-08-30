#pragma once

#include <functional>
#include <utility>

namespace gui::architecture {
template <typename Event>
using EventHandler = std::function<void(const Event &)>;

// A local, typed connection from a child view or feature to its owner.
// Event sinks are deliberately synchronous and are not a global event bus.
template <typename Event> class EventSink {
public:
  EventSink() = default;
  explicit EventSink(EventHandler<Event> handler)
      : handler_(std::move(handler)) {}

  void Emit(const Event &event) const {
    if (handler_) {
      handler_(event);
    }
  }

  [[nodiscard]] bool IsConnected() const { return static_cast<bool>(handler_); }

private:
  EventHandler<Event> handler_;
};
} // namespace gui::architecture
