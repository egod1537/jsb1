#include "flightui/controls/Separator.hpp"

#include "flightui/core/UIElementFactory.hpp"

#include <imgui.h>

namespace FlightUI {
UIElement Separator() {
  return CreateElement([] { ImGui::Separator(); });
}
} // namespace FlightUI
