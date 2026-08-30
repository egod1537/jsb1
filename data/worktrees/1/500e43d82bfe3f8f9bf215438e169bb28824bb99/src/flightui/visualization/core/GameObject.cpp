#include "flightui/visualization/core/GameObject.hpp"

#include <utility>

namespace viz {
GameObject::GameObject(std::string name) : name_(std::move(name)) {}

GameObject::~GameObject() = default;

void GameObject::Tick(const TickContext &context) {
  for (const auto &component : components_) {
    component->OnTick(context);
  }
}

void GameObject::Render(RenderContext &context) const {
  for (const auto &component : components_) {
    component->Render(context);
  }
}
} // namespace viz
