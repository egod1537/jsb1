#pragma once

#include "flightui/visualization/core/Component.hpp"
#include "flightui/visualization/core/Transform.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace viz {
class GameObject {
public:
  // Lifetime
  explicit GameObject(std::string name);
  ~GameObject();

  GameObject(const GameObject &other) = delete;
  GameObject &operator=(const GameObject &other) = delete;

  // Identity and transform
  const std::string &GetName() const { return name_; }
  Transform &GetTransform() { return transform_; }
  const Transform &GetTransform() const { return transform_; }

  // Components
  template <typename T, typename... Args> T &AddComponent(Args &&...args) {
    static_assert(std::is_base_of_v<Component, T>,
        "T must inherit from viz::Component");

    auto component = std::make_unique<T>(std::forward<Args>(args)...);
    component->SetGameObject(this);
    T &componentRef = *component;
    components_.push_back(std::move(component));
    return componentRef;
  }

  // Frame lifecycle
  void Tick(const TickContext &context);
  void Render(RenderContext &context) const;

private:
  // Object state
  std::string name_;
  Transform transform_;

  // Component ownership
  std::vector<std::unique_ptr<Component>> components_;
};
} // namespace viz
