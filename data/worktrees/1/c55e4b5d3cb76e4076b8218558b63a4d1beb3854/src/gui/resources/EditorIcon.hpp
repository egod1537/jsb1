#pragma once

#include <imgui.h>
#include <string>

namespace gui {
struct EditorIconHandle {
  ImTextureID texture = ImTextureID_Invalid;
  ImVec2 size{};

  bool IsValid() const {
    return texture != ImTextureID_Invalid && size.x > 0.0F && size.y > 0.0F;
  }
};

struct EditorIconInfo {
  std::string name;
  std::string relativeName;
};

namespace EditorIconAliases {
inline constexpr const char *GNC = "d_SettingsIcon";
inline constexpr const char *Scenario = "d_TextAsset Icon";
inline constexpr const char *FlightViz = "d_SceneViewCamera";
inline constexpr const char *ShadowAircraft = "d_scenevis_visible-mixed";
inline constexpr const char *ViewOptions = "d_SceneViewVisibility";
inline constexpr const char *CameraView = "d_ViewToolOrbit";
inline constexpr const char *LayoutDropdown = "d_icon dropdown";
inline constexpr const char *Monitor = "d_UnityEditor.ProfilerWindow";
inline constexpr const char *Simulation = "d_PlayButton";
inline constexpr const char *Linearization =
    "d_UnityEditor.Graphs.AnimatorControllerTool";
} // namespace EditorIconAliases
} // namespace gui
