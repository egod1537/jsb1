#include "flightui/visualization/core/Component.hpp"

#include "flightui/visualization/core/GameObject.hpp"

namespace viz {
Component::~Component() = default;

GameObject &Component::GetGameObject() { return *gameObject_; }

const GameObject &Component::GetGameObject() const { return *gameObject_; }

Transform &Component::GetTransform() { return gameObject_->GetTransform(); }

const Transform &Component::GetTransform() const {
  return gameObject_->GetTransform();
}

void Component::SetGameObject(GameObject *gameObject) {
  gameObject_ = gameObject;
}
} // namespace viz
