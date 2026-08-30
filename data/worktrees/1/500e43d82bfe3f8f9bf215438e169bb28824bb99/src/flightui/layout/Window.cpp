#include "flightui/layout/Window.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIRenderHelpers.hpp"
#include "flightui/core/UIScale.hpp"

#include <imgui.h>

#include <utility>

namespace FlightUI {
class WindowBuilder::Impl {
public:
  std::string Title;
  bool *Open = nullptr;
  ImGuiWindowFlags Flags = ImGuiWindowFlags_None;
  Vector2 InitialSize;
  Vector2 InitialPosition;
  bool HasInitialSize = false;
  bool HasInitialPosition = false;
  bool Enabled = true;
  bool Visible = true;
  std::string Tooltip;
  std::string Id;
};

namespace {
ImVec2 ToImVec2(Vector2 value) { return ImVec2(value.X, value.Y); }
} // namespace

WindowBuilder::WindowBuilder(std::string title)
    : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Title = std::move(title);
}

WindowBuilder::WindowBuilder(const WindowBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

WindowBuilder::WindowBuilder(WindowBuilder &&other) noexcept = default;

WindowBuilder &WindowBuilder::operator=(const WindowBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

WindowBuilder &WindowBuilder::operator=(
    WindowBuilder &&other) noexcept = default;

WindowBuilder::~WindowBuilder() = default;

WindowBuilder &WindowBuilder::SetOpen(bool &isOpen) {
  m_Impl->Open = &isOpen;
  return *this;
}

WindowBuilder &WindowBuilder::SetFlags(ImGuiWindowFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

WindowBuilder &WindowBuilder::SetInitialSize(Vector2 size) {
  m_Impl->InitialSize = size;
  m_Impl->HasInitialSize = true;
  return *this;
}

WindowBuilder &WindowBuilder::SetInitialPosition(Vector2 position) {
  m_Impl->InitialPosition = position;
  m_Impl->HasInitialPosition = true;
  return *this;
}

WindowBuilder &WindowBuilder::SetEnabled(bool enabled) {
  m_Impl->Enabled = enabled;
  return *this;
}

WindowBuilder &WindowBuilder::SetVisible(bool visible) {
  m_Impl->Visible = visible;
  return *this;
}

WindowBuilder &WindowBuilder::SetTooltip(std::string tooltip) {
  m_Impl->Tooltip = std::move(tooltip);
  return *this;
}

WindowBuilder &WindowBuilder::SetId(std::string id) {
  m_Impl->Id = std::move(id);
  return *this;
}

WindowBuilder &WindowBuilder::Open(bool &isOpen) { return SetOpen(isOpen); }

WindowBuilder &WindowBuilder::Flags(ImGuiWindowFlags flags) {
  return SetFlags(flags);
}

WindowBuilder &WindowBuilder::InitialSize(Vector2 size) {
  return SetInitialSize(size);
}

WindowBuilder &WindowBuilder::InitialPosition(Vector2 position) {
  return SetInitialPosition(position);
}

WindowBuilder &WindowBuilder::Enabled(bool enabled) {
  return SetEnabled(enabled);
}

WindowBuilder &WindowBuilder::Visible(bool visible) {
  return SetVisible(visible);
}

WindowBuilder &WindowBuilder::Tooltip(std::string tooltip) {
  return SetTooltip(std::move(tooltip));
}

WindowBuilder &WindowBuilder::Id(std::string id) {
  return SetId(std::move(id));
}

UIElement WindowBuilder::operator[](UIElement child) const {
  Children children;
  children.push_back(std::move(child));
  return (*this)[std::move(children)];
}

UIElement WindowBuilder::operator[](ChildrenBuilder children) const {
  return (*this)[std::move(children).TakeChildren()];
}

UIElement WindowBuilder::operator[](Children children) const {
  Impl state = *m_Impl;
  UIElement element = CreateElement([state, children = std::move(children)] {
    if (!state.Visible) {
      return;
    }

    bool localOpen = state.Open == nullptr || *state.Open;
    if (!localOpen) {
      return;
    }

    if (state.HasInitialSize) {
      ImGui::SetNextWindowSize(ToImVec2(UiSize(state.InitialSize)),
          ImGuiCond_FirstUseEver);
    }

    if (state.HasInitialPosition) {
      ImGui::SetNextWindowPos(ToImVec2(Ui(state.InitialPosition)),
          ImGuiCond_FirstUseEver);
    }

    Internal::DisabledScope disabledScope(!state.Enabled);
    const std::string title =
        state.Id.empty() ? state.Title : state.Title + "###" + state.Id;
    const bool isVisible = ImGui::Begin(title.c_str(),
        state.Open == nullptr ? nullptr : &localOpen,
        state.Flags);

    if (!state.Tooltip.empty()
        && ImGui::IsWindowHovered(ImGuiHoveredFlags_DelayNormal)) {
      ImGui::SetTooltip("%s", state.Tooltip.c_str());
    }

    if (isVisible) {
      for (const UIElement &childElement : children) {
        childElement.Render();
      }
    }

    ImGui::End();

    if (state.Open != nullptr) {
      *state.Open = localOpen;
    }
  });

  element.Render();
  return element;
}

WindowBuilder Window(std::string title) {
  return WindowBuilder(std::move(title));
}
} // namespace FlightUI
