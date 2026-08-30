#include "messaging/MessageBus.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace {
struct TestMessage {
  int value = 0;
};

void TestOneSubscriberReceivesMessage() {
  application::messaging::MessageBus bus;
  int received = 0;
  auto subscription = bus.Subscribe<TestMessage>(
      [&received](const TestMessage &message) { received = message.value; });

  bus.Publish(TestMessage{.value = 42});
  assert(received == 42);
}

void TestMultipleSubscribersReceiveMessage() {
  application::messaging::MessageBus bus;
  int first = 0;
  int second = 0;
  auto firstSubscription = bus.Subscribe<TestMessage>(
      [&first](const TestMessage &message) { first += message.value; });
  auto secondSubscription = bus.Subscribe<TestMessage>(
      [&second](const TestMessage &message) { second += message.value; });

  bus.Publish(TestMessage{.value = 3});
  assert(first == 3);
  assert(second == 3);
}

void TestExplicitUnsubscribeWorks() {
  application::messaging::MessageBus bus;
  int count = 0;
  auto subscription =
      bus.Subscribe<TestMessage>([&count](const TestMessage &) { ++count; });
  subscription.Reset();

  bus.Publish(TestMessage{});
  assert(count == 0);
}

void TestSubscriptionDestructionUnsubscribes() {
  application::messaging::MessageBus bus;
  int count = 0;
  {
    auto subscription =
        bus.Subscribe<TestMessage>([&count](const TestMessage &) { ++count; });
  }

  bus.Publish(TestMessage{});
  assert(count == 0);
}

void TestPublishingWithoutSubscribersIsSafe() {
  application::messaging::MessageBus bus;
  bus.Publish(TestMessage{.value = 7});
}

void TestSubscriberSelfRemovalDuringPublishIsSafe() {
  application::messaging::MessageBus bus;
  int calls = 0;
  application::messaging::Subscription subscription;
  subscription =
      bus.Subscribe<TestMessage>([&subscription, &calls](const TestMessage &) {
        ++calls;
        subscription.Reset();
      });

  bus.Publish(TestMessage{});
  bus.Publish(TestMessage{});
  assert(calls == 1);
}

void TestRemovingAnotherSubscriberDuringPublishSkipsIt() {
  application::messaging::MessageBus bus;
  int removedSubscriberCalls = 0;
  application::messaging::Subscription removedSubscription;
  auto removingSubscription =
      bus.Subscribe<TestMessage>([&removedSubscription](const TestMessage &) {
        removedSubscription.Reset();
      });
  removedSubscription = bus.Subscribe<TestMessage>(
      [&removedSubscriberCalls](
          const TestMessage &) { ++removedSubscriberCalls; });

  bus.Publish(TestMessage{});
  assert(removedSubscriberCalls == 0);
  bus.Publish(TestMessage{});
  assert(removedSubscriberCalls == 0);
}

void TestMessageBusDestructionBeforeSubscriptionIsSafe() {
  application::messaging::Subscription subscription;
  {
    auto bus = std::make_unique<application::messaging::MessageBus>();
    subscription = bus->Subscribe<TestMessage>([](const TestMessage &) {});
  }

  subscription.Reset();
  assert(!subscription);
}

void TestSubscriptionMoveConstructionAndAssignment() {
  application::messaging::MessageBus bus;
  int movedSubscriberCalls = 0;
  int replacedSubscriberCalls = 0;

  auto source = bus.Subscribe<TestMessage>(
      [&movedSubscriberCalls](const TestMessage &) { ++movedSubscriberCalls; });
  application::messaging::Subscription moved(std::move(source));
  assert(!source);
  bus.Publish(TestMessage{});
  assert(movedSubscriberCalls == 1);

  auto assigned = bus.Subscribe<TestMessage>(
      [&replacedSubscriberCalls](
          const TestMessage &) { ++replacedSubscriberCalls; });
  assigned = std::move(moved);
  assert(!moved);
  bus.Publish(TestMessage{});
  assert(movedSubscriberCalls == 2);
  assert(replacedSubscriberCalls == 0);
}
} // namespace

int main() {
  TestOneSubscriberReceivesMessage();
  TestMultipleSubscribersReceiveMessage();
  TestExplicitUnsubscribeWorks();
  TestSubscriptionDestructionUnsubscribes();
  TestPublishingWithoutSubscribersIsSafe();
  TestSubscriberSelfRemovalDuringPublishIsSafe();
  TestRemovingAnotherSubscriberDuringPublishSkipsIt();
  TestMessageBusDestructionBeforeSubscriptionIsSafe();
  TestSubscriptionMoveConstructionAndAssignment();
  return 0;
}
