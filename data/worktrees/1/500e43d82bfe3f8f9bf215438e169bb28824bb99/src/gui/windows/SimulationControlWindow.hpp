#pragma once

#include "gui/features/editor/EditorPlatformController.hpp"
#include "gui/features/simulation/ScenarioSetupPopup.hpp"
#include "gui/features/simulation/SimulationController.hpp"
#include "gui/Window.hpp"

#include <array>
#include <string>

namespace gui {
class EditorIconRegistry;
class ScenarioController;

class SimulationControlWindow final : public Window {
public:
  // Lifetime and layout
  SimulationControlWindow(SimulationController &simulation,
      ScenarioController &scenario, EditorPlatformController &editorPlatform,
      EditorIconRegistry &icons);
  static float GetReservedHeight();

protected:
  // Window configuration and rendering
  void PrepareWindow() override;
  ImGuiWindowFlags GetWindowFlags() const override;
  void OnRender(const sim::SimulationSnapshot &snapshot) override;

private:
  // Simulation transport controls
  void HandleTransportShortcut();
  void HandleSimulationSpeedShortcut(bool enabled);

  // Layout preset controls
  void HandleLayoutShortcuts();
  void DrawLayoutDropdown(float width);
  void DrawLayoutDialogs();
  void DrawSaveLayoutDialog();
  void DrawManageLayoutsDialog();
  void ImportLayout();
  void ExportLayout(const LayoutPresetId &id);
  void SetLayoutFeedback(std::string message, bool isError = false);
  std::string GetLayoutButtonLabel() const;

  // Dependencies
  SimulationController &simulation_;
  ScenarioSetupPopup scenarioPopup_;
  EditorPlatformController &editorPlatform_;
  EditorIconRegistry &icons_;

  // Layout dialog state
  std::array<char, 257> layoutNameInput_{};
  LayoutPresetId selectedLayoutId_;
  std::string layoutFeedback_;
  bool layoutFeedbackIsError_ = false;
  bool openSaveLayoutDialog_ = false;
  bool openManageLayoutsDialog_ = false;
  bool manageLayoutsVisible_ = false;
  bool renameLayout_ = false;
};
} // namespace gui
