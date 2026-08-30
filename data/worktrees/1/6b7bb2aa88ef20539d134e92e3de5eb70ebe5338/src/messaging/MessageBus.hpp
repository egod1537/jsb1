#pragma once

#include "messaging/Subscription.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace application::messaging {
// A small synchronous, in-process type-based message dispatcher.
//
// Publish invokes callbacks before returning, on the caller thread. The bus
// owns no worker threads or queues; synchronous request/result flows rely on
// this behavior. Cross-thread publishing, subscription changes, and subscriber
// lifetime must be externally serialized. This class is not intended to be a
// general asynchronous event framework.
class MessageBus {
public:
  MessageBus();
  ~MessageBus();

  MessageBus(const MessageBus &) = delete;
  MessageBus &operator=(const MessageBus &) = delete;

  // Synchronous type-based messaging
  template <typename Message>
  Subscription Subscribe(std::function<void(const Message &)> callback) {
    const std::type_index messageType(typeid(Message));
    auto active = std::make_shared<std::atomic_bool>(true);
    std::size_t subscriptionId = 0;
    {
      std::scoped_lock lock(state_->mutex);
      subscriptionId = state_->nextSubscriptionId++;
      state_->callbacks[messageType].push_back({
          .id = subscriptionId,
          .active = active,
          .callback =
              [callback = std::move(callback)](const void *message) {
                callback(*static_cast<const Message *>(message));
              },
      });
    }

    std::weak_ptr<State> weakState = state_;
    return Subscription([weakState, messageType, subscriptionId, active] {
      active->store(false, std::memory_order_release);
      const std::shared_ptr<State> state = weakState.lock();
      if (state == nullptr) {
        return;
      }
      std::scoped_lock lock(state->mutex);
      const auto channel = state->callbacks.find(messageType);
      if (channel == state->callbacks.end()) {
        return;
      }
      std::erase_if(channel->second,
          [subscriptionId](const CallbackEntry &entry) {
            return entry.id == subscriptionId;
          });
      if (channel->second.empty()) {
        state->callbacks.erase(channel);
      }
    });
  }

  template <typename Message> void Publish(const Message &message) const {
    std::vector<CallbackEntry> callbacks;
    {
      std::scoped_lock lock(state_->mutex);
      const auto channel =
          state_->callbacks.find(std::type_index(typeid(Message)));
      if (channel == state_->callbacks.end()) {
        return;
      }
      callbacks = channel->second;
    }

    for (const CallbackEntry &entry : callbacks) {
      if (entry.active->load(std::memory_order_acquire)) {
        entry.callback(&message);
      }
    }
  }

private:
  struct CallbackEntry {
    std::size_t id = 0;
    std::shared_ptr<std::atomic_bool> active;
    std::function<void(const void *)> callback;
  };

  struct State {
    std::mutex mutex;
    std::unordered_map<std::type_index, std::vector<CallbackEntry>> callbacks;
    std::size_t nextSubscriptionId = 1;
  };

  std::shared_ptr<State> state_;
};
} // namespace application::messaging
