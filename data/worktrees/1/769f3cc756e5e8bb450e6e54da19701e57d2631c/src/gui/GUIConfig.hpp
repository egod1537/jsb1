#pragma once

#include <string>

namespace gui {
struct GUIConfig {
  int windowWidth = 1280;
  int windowHeight = 720;
  std::string windowTitle = "JSB Flight Console";

  double renderHz = 60.0;

  double GetRenderDT() const { return renderHz > 0.0 ? 1.0 / renderHz : 0.0; }
};
} // namespace gui
