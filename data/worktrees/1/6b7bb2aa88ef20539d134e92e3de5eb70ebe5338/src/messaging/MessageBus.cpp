#include "messaging/MessageBus.hpp"

namespace application::messaging {
MessageBus::MessageBus() : state_(std::make_shared<State>()) {}
MessageBus::~MessageBus() = default;
} // namespace application::messaging
