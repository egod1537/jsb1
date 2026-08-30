#pragma once

#include <functional>
#include <optional>

namespace gui {
class Toolbar {
public:
  using RenderCallback = std::function<void()>;

  // Slot composition
  Toolbar &Left(float widthPixels, RenderCallback render);
  Toolbar &Center(float widthPixels, RenderCallback render);
  Toolbar &Right(float widthPixels, RenderCallback render);

  // Layout rendering
  void Render() const;

private:
  struct Slot {
    float widthPixels = 0.0F;
    RenderCallback render;
  };

  static void RenderSlot(const std::optional<Slot> &slot, float x, float y);

  std::optional<Slot> left_;
  std::optional<Slot> center_;
  std::optional<Slot> right_;
};
} // namespace gui
