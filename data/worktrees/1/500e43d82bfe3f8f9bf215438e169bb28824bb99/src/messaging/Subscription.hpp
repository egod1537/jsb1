#pragma once

#include <functional>
#include <utility>

namespace application::messaging {
class MessageBus;

class Subscription {
public:
  Subscription() = default;
  ~Subscription();

  Subscription(const Subscription &) = delete;
  Subscription &operator=(const Subscription &) = delete;
  Subscription(Subscription &&other) noexcept;
  Subscription &operator=(Subscription &&other) noexcept;

  void Reset();
  explicit operator bool() const { return static_cast<bool>(unsubscribe_); }

private:
  friend class MessageBus;
  explicit Subscription(std::function<void()> unsubscribe)
      : unsubscribe_(std::move(unsubscribe)) {}

  std::function<void()> unsubscribe_;
};
} // namespace application::messaging
