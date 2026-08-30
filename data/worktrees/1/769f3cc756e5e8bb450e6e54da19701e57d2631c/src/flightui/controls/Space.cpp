#include "flightui/controls/Space.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

namespace FlightUI {
UIElement Space(float size) {
  return CreateElement([size] { ImGui::Dummy(ImVec2(0.0F, Ui(size))); });
}

UIElement HorizontalSpace(float size) {
  return CreateElement([size] { ImGui::Dummy(ImVec2(Ui(size), 0.0F)); });
}
} // namespace FlightUI
