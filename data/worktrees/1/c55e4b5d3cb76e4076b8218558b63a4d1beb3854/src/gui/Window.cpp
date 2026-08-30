#include "gui/Window.hpp"

#include "gui/resources/EditorIconRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <utility>

namespace gui {
namespace {
constexpr const char *IconLabelPadding = "     ";
constexpr float IconHeightRatio = 0.9F;

std::string BuildWindowLabel(const std::string &title,
    const std::string &windowId) {
  return title == windowId ? title : title + "###" + windowId;
}

std::string BuildIconTitle(const std::string &title,
    const std::string &windowId) {
  return std::string(IconLabelPadding) + title + "###" + windowId;
}
} // namespace

Window::Window(std::string title, std::string iconName, std::string windowId)
    : title_(std::move(title)),
      windowId_(windowId.empty() ? title_ : std::move(windowId)),
      windowLabel_(BuildWindowLabel(title_, windowId_)),
      iconTitle_(BuildIconTitle(title_, windowId_)),
      iconName_(std::move(iconName)) {}

Window::~Window() = default;

void Window::OnTick(const GUIFrameContext &context) {
  if (!visible_) {
    return;
  }

  const bool wasVisible = visible_;
  PrepareWindow();
  const EditorIconHandle icon =
      iconName_.empty() ? EditorIconHandle{} : context.icons.Get(iconName_);
  const char *windowLabel =
      icon.IsValid() ? iconTitle_.c_str() : windowLabel_.c_str();
  const bool contentVisible =
      ImGui::Begin(windowLabel, &visible_, GetWindowFlags());
  if (icon.IsValid()) {
    DrawTitleIcon(icon);
  }
  if (contentVisible) {
    OnRender(context.simulation);
  }
  ImGui::End();
  if (visible_ != wasVisible) {
    ImGui::MarkIniSettingsDirty();
  }
}

void Window::PrepareWindow() {}

ImGuiWindowFlags Window::GetWindowFlags() const {
  return ImGuiWindowFlags_None;
}

void Window::OnRender() {}

void Window::OnRender(const sim::SimulationSnapshot &) { OnRender(); }

void Window::DrawTitleIcon(const EditorIconHandle &icon) const {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (window == nullptr) {
    return;
  }

  ImRect titleRect;
  ImDrawList *drawList = nullptr;
  float iconX = 0.0F;
  if (window->DockNode != nullptr && window->DockNode->TabBar != nullptr
      && window->DC.DockTabItemRect.GetWidth() > 0.0F) {
    titleRect = window->DC.DockTabItemRect;
    drawList = window->DockNode->HostWindow != nullptr
                   ? window->DockNode->HostWindow->DrawList
                   : window->DrawList;
    iconX = titleRect.Min.x + window->DockNode->TabBar->FramePadding.x;
  } else {
    if ((window->Flags & ImGuiWindowFlags_NoTitleBar) != 0) {
      return;
    }

    const ImGuiStyle &style = ImGui::GetStyle();
    titleRect = window->TitleBarRect();
    drawList = window->DrawList;

    float leftPadding = style.FramePadding.x;
    float rightPadding = style.FramePadding.x;
    const bool hasCloseButton = window->HasCloseButton;
    const bool hasCollapseButton =
        (window->Flags & ImGuiWindowFlags_NoCollapse) == 0
        && style.WindowMenuButtonPosition != ImGuiDir_None;
    if (hasCloseButton) {
      rightPadding += ImGui::GetFontSize() + style.ItemInnerSpacing.x;
    }
    if (hasCollapseButton && style.WindowMenuButtonPosition == ImGuiDir_Right) {
      rightPadding += ImGui::GetFontSize() + style.ItemInnerSpacing.x;
    }
    if (hasCollapseButton && style.WindowMenuButtonPosition == ImGuiDir_Left) {
      leftPadding += ImGui::GetFontSize() + style.ItemInnerSpacing.x;
    }
    if (leftPadding > style.FramePadding.x) {
      leftPadding += style.ItemInnerSpacing.x;
    }
    if (rightPadding > style.FramePadding.x) {
      rightPadding += style.ItemInnerSpacing.x;
    }

    const float layoutMinX =
        titleRect.Min.x + window->WindowBorderSize + leftPadding;
    const float layoutMaxX =
        titleRect.Max.x - window->WindowBorderSize - rightPadding;
    const float titleWidth =
        ImGui::CalcTextSize(iconTitle_.c_str(), nullptr, true).x;
    iconX = layoutMinX
            + std::max(layoutMaxX - layoutMinX - titleWidth, 0.0F)
                  * style.WindowTitleAlign.x;
  }

  if (drawList == nullptr || titleRect.GetHeight() <= 2.0F) {
    return;
  }

  const float iconHeight = std::min(ImGui::GetFontSize() * IconHeightRatio,
      titleRect.GetHeight() - 2.0F);
  const float iconWidth = iconHeight * icon.size.x / icon.size.y;
  const float iconY =
      titleRect.Min.y + (titleRect.GetHeight() - iconHeight) * 0.5F;
  const ImVec2 iconMin{std::floor(iconX), std::floor(iconY)};
  const ImVec2 iconMax{iconMin.x + iconWidth, iconMin.y + iconHeight};

  drawList->PushClipRect(titleRect.Min, titleRect.Max, true);
  drawList->AddImage(ImTextureRef(icon.texture),
      iconMin,
      iconMax,
      ImVec2(0.0F, 0.0F),
      ImVec2(1.0F, 1.0F),
      ImGui::GetColorU32(ImGuiCol_Text));
  drawList->PopClipRect();
}
} // namespace gui
