#include "flightui/core/UICommon.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

namespace FlightUI::Internal {
IdScope::IdScope(const std::string &id) : m_Active(!id.empty()) {
  if (m_Active) {
    ImGui::PushID(id.c_str());
  }
}

IdScope::~IdScope() {
  if (m_Active) {
    ImGui::PopID();
  }
}

DisabledScope::DisabledScope(bool disabled) : m_Active(disabled) {
  if (m_Active) {
    ImGui::BeginDisabled();
  }
}

DisabledScope::~DisabledScope() {
  if (m_Active) {
    ImGui::EndDisabled();
  }
}

ItemWidthScope::ItemWidthScope(float width) : m_Active(width > 0.0F) {
  if (m_Active) {
    ImGui::PushItemWidth(Ui(width));
  }
}

ItemWidthScope::~ItemWidthScope() {
  if (m_Active) {
    ImGui::PopItemWidth();
  }
}

void ShowTooltipIfHovered(const std::string &tooltip) {
  if (tooltip.empty() || !ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    return;
  }

  ImGui::SetTooltip("%s", tooltip.c_str());
}
} // namespace FlightUI::Internal

namespace FlightUI {
double GetTime() { return ImGui::GetTime(); }

namespace {
ImGuiKey ToImGuiKey(Key key) {
  switch (key) {
  case Key::A:
    return ImGuiKey_A;
  case Key::D:
    return ImGuiKey_D;
  case Key::E:
    return ImGuiKey_E;
  case Key::F:
    return ImGuiKey_F;
  case Key::Q:
    return ImGuiKey_Q;
  case Key::R:
    return ImGuiKey_R;
  case Key::S:
    return ImGuiKey_S;
  case Key::W:
    return ImGuiKey_W;
  }

  return ImGuiKey_None;
}
} // namespace

bool IsKeyPressed(Key key, bool repeat) {
  return ImGui::IsKeyPressed(ToImGuiKey(key), repeat);
}

bool IsCurrentWindowFocused() {
  return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
}

bool WantsTextInput() { return ImGui::GetIO().WantTextInput; }
} // namespace FlightUI
