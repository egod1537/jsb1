#include "flightui/controls/Heading.hpp"

#include "flightui/core/UIElementFactory.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
UIElement Heading(std::string text) {
  return CreateElement([text = std::move(text)] {
#if IMGUI_VERSION_NUM >= 18900
    ImGui::SeparatorText(text.c_str());
#else
    ImGui::TextUnformatted(text.c_str());
    ImGui::Separator();
#endif
  });
}
} // namespace FlightUI
