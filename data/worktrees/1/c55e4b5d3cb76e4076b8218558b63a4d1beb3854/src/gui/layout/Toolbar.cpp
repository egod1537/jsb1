#include "gui/layout/Toolbar.hpp"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace gui {
Toolbar &Toolbar::Left(float widthPixels, RenderCallback render) {
  left_ = Slot{std::max(widthPixels, 0.0F), std::move(render)};
  return *this;
}

Toolbar &Toolbar::Center(float widthPixels, RenderCallback render) {
  center_ = Slot{std::max(widthPixels, 0.0F), std::move(render)};
  return *this;
}

Toolbar &Toolbar::Right(float widthPixels, RenderCallback render) {
  right_ = Slot{std::max(widthPixels, 0.0F), std::move(render)};
  return *this;
}

void Toolbar::Render() const {
  const float contentStartX = ImGui::GetCursorPosX();
  const float contentY = ImGui::GetCursorPosY();
  const float contentWidth = ImGui::GetContentRegionAvail().x;

  RenderSlot(left_, contentStartX, contentY);
  if (center_.has_value()) {
    RenderSlot(center_,
        contentStartX
            + std::max((contentWidth - center_->widthPixels) * 0.5F, 0.0F),
        contentY);
  }
  if (right_.has_value()) {
    RenderSlot(right_,
        contentStartX + std::max(contentWidth - right_->widthPixels, 0.0F),
        contentY);
  }
}

void Toolbar::RenderSlot(const std::optional<Slot> &slot, float x, float y) {
  if (!slot.has_value() || !slot->render) {
    return;
  }

  ImGui::SetCursorPos(ImVec2(x, y));
  slot->render();
}
} // namespace gui
