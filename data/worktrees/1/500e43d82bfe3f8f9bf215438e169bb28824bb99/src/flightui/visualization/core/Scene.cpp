#include "flightui/visualization/core/Scene.hpp"

#include <utility>

namespace viz {
GameObject &Scene::CreateGameObject(std::string name) {
  auto gameObject = std::make_unique<GameObject>(std::move(name));
  GameObject &gameObjectRef = *gameObject;
  gameObjects_.push_back(std::move(gameObject));
  return gameObjectRef;
}

void Scene::Clear() { gameObjects_.clear(); }

void Scene::Tick(const FrameSnapshot &snapshot) {
  const TickContext context{snapshot};
  for (const auto &gameObject : gameObjects_) {
    gameObject->Tick(context);
  }
}

void Scene::Render(RenderContext &context) const {
  for (const auto &gameObject : gameObjects_) {
    gameObject->Render(context);
  }
}
} // namespace viz
