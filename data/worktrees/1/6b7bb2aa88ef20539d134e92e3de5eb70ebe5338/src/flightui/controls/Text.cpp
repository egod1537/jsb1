#include "flightui/controls/Text.hpp"

#include "flightui/core/UIElementFactory.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
UIElement Text(std::string text) {
  return CreateElement(
      [text = std::move(text)] { ImGui::TextUnformatted(text.c_str()); });
}

UIElement TextDisabled(std::string text) {
  return CreateElement(
      [text = std::move(text)] { ImGui::TextDisabled("%s", text.c_str()); });
}

UIElement TextWrapped(std::string text) {
  return CreateElement(
      [text = std::move(text)] { ImGui::TextWrapped("%s", text.c_str()); });
}
} // namespace FlightUI
