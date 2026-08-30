#include "messaging/Subscription.hpp"

namespace application::messaging {
Subscription::~Subscription() { Reset(); }

Subscription::Subscription(Subscription &&other) noexcept
    : unsubscribe_(std::exchange(other.unsubscribe_, {})) {}

Subscription &Subscription::operator=(Subscription &&other) noexcept {
  if (this != &other) {
    Reset();
    unsubscribe_ = std::exchange(other.unsubscribe_, {});
  }
  return *this;
}

void Subscription::Reset() {
  if (!unsubscribe_) {
    return;
  }
  auto unsubscribe = std::move(unsubscribe_);
  unsubscribe();
}
} // namespace application::messaging
