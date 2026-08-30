#pragma once

#include "flightui/visualization/core/VizContext.hpp"

namespace viz {
class GameObject;
class Transform;

class Component {
public:
  virtual ~Component();

  // Owner-provided state
  GameObject &GetGameObject();
  const GameObject &GetGameObject() const;
  Transform &GetTransform();
  const Transform &GetTransform() const;

  // Frame lifecycle
  virtual void OnTick(const TickContext &context) {}
  virtual void Render(RenderContext &context) const {}

private:
  friend class GameObject;

  // Owner assignment
  void SetGameObject(GameObject *gameObject);

  // Scene attachment
  GameObject *gameObject_ = nullptr;
};
} // namespace viz
