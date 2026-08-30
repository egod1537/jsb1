#pragma once

#include "flightui/visualization/core/GameObject.hpp"

#include <memory>
#include <string>
#include <vector>

namespace viz {
class Scene {
public:
  GameObject &CreateGameObject(std::string name);
  void Clear();

  void Tick(const FrameSnapshot &snapshot);
  void Render(RenderContext &context) const;

private:
  std::vector<std::unique_ptr<GameObject>> gameObjects_;
};
} // namespace viz
