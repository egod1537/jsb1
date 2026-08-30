#pragma once

#include <vector>

namespace gui {
class Window;

class EditorWindowStateSettings {
public:
  // ImGui settings integration
  bool Register(std::vector<Window *> &windows);

private:
  std::vector<Window *> *windows_ = nullptr;
};
} // namespace gui
