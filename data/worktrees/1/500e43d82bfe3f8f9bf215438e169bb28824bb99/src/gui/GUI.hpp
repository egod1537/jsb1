#pragma once

#include "gui/Component.hpp"
#include "gui/GUIConfig.hpp"
#include "gui/Window.hpp"
#include "gui/layout/EditorLayoutManager.hpp"
#include "gui/layout/EditorWindowStateSettings.hpp"
#include "gui/platform/FileDialogService.hpp"
#include "gui/resources/EditorIconRegistry.hpp"
#include "sim/runtime/SimulationContracts.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace gui {
class FlightVizWindow;
class EditorPlatformController;
class GNCController;
class LinearizationController;
class MonitorController;
class ScenarioController;
class SimulationController;

} // namespace gui

namespace application {
class SimulationMessageClient;
}

namespace gui {

class GUI {
public:
  // Lifetime and frame loop
  explicit GUI(GUIConfig config = {});
  ~GUI();

  GUI(const GUI &other) = delete;
  GUI &operator=(const GUI &other) = delete;

  bool Start();
  void Tick();
  void Exit();

  // Window state
  bool ShouldClose() const;
  void RequestClose();

  const GUIConfig &GetConfig() const { return config_; }
  void ResetEditorLayoutToDefault();

  // Application control
  void SetSimulationMessageClient(application::SimulationMessageClient *client);
  // UI registration
  void RegisterComponent(std::unique_ptr<Component> component);
  void RegisterWindow(std::unique_ptr<Window> window);

  template <typename T, typename... Args> T &RegisterComponent(Args &&...args) {
    static_assert(std::is_base_of_v<Component, T>,
        "T must inherit from gui::Component");

    auto component = std::make_unique<T>(std::forward<Args>(args)...);
    T &componentRef = *component;
    RegisterComponent(std::move(component));
    return componentRef;
  }

  template <typename T, typename... Args> T &RegisterWindow(Args &&...args) {
    static_assert(std::is_base_of_v<Window, T>,
        "T must inherit from gui::Window");

    auto window = std::make_unique<T>(std::forward<Args>(args)...);
    T &windowRef = *window;
    RegisterWindow(std::move(window));
    return windowRef;
  }

private:
  // Frame lifecycle
  void BeginFrame();
  void RenderFrame();
  void EndFrame();

  // Rendering
  void UpdateUIScale(bool force = false);
  void RenderDockSpace();
  void InitializeDefaultDockLayout(ImGuiID dockSpaceId, ImVec2 dockSpaceSize);
  void RenderMainMenuBar();
  void RenderSimulationMenu();
  void RenderWindowMenu();

  // Component lifecycle
  void RegisterFeatureTree();
  void StartComponents();
  void TickComponents();

  // Platform state
  GLFWwindow *window_ = nullptr;
  bool initialized_ = false;
  bool glfwInitialized_ = false;
  bool imguiContextCreated_ = false;
  bool glfwBackendInitialized_ = false;
  bool openGlBackendInitialized_ = false;

  // Responsive UI state
  ImGuiStyle baseImGuiStyle_;
  ImPlotStyle baseImPlotStyle_;
  float appliedUIScale_ = 0.0F;

  // UI ownership
  EditorIconRegistry editorIcons_;
  ImGuiEditorLayoutBackend editorLayoutBackend_;
  EditorLayoutManager editorLayoutManager_;
  NativeFileDialogService fileDialogService_;
  std::string workspaceIniPathString_;
  std::vector<std::unique_ptr<Component>> components_;
  std::vector<Window *> windows_;
  EditorWindowStateSettings windowStateSettings_;
  FlightVizWindow *primaryFlightVizWindow_ = nullptr;
  FlightVizWindow *baselineFlightVizWindow_ = nullptr;
  bool defaultDockLayoutInitialized_ = false;
  bool featureTreeRegistered_ = false;

  // Application dependencies
  application::SimulationMessageClient *simulationMessageClient_ = nullptr;
  std::unique_ptr<SimulationController> simulationController_;
  std::unique_ptr<ScenarioController> scenarioController_;
  std::unique_ptr<EditorPlatformController> editorPlatformController_;
  std::unique_ptr<GNCController> gncController_;
  std::unique_ptr<LinearizationController> linearizationController_;
  std::unique_ptr<MonitorController> monitorController_;
  sim::SimulationSnapshot simulationSnapshot_;

  // Configuration
  GUIConfig config_;
};
} // namespace gui
