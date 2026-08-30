#include "GUI.hpp"
#include "gui/features/editor/EditorPlatformController.hpp"
#include "gui/features/monitor/MonitorController.hpp"
#include "gui/features/simulation/ScenarioController.hpp"
#include "messaging/SimulationMessageClient.hpp"
#include "gui/windows/EditorIconBrowserWindow.hpp"
#include "gui/windows/GNCWindow.hpp"
#include "gui/windows/LinearizationWindow.hpp"
#include "gui/windows/ScenarioWindow.hpp"
#include "gui/windows/SimulationControlWindow.hpp"
#include "gui/windows/SimulationWindow.hpp"
#include "gui/windows/monitor/FlightDataMonitorWindow.hpp"
#include "gui/windows/viz/FlightVizWindow.hpp"
#include "flightui/core/Theme.hpp"
#include "flightui/core/UIFont.hpp"
#include "flightui/core/UIScale.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <utility>

namespace {
constexpr const char *GlslVersion = "#version 130";
constexpr int SwapInterval = 1;
constexpr float UIScaleChangeThreshold = 0.02F;

ImVec2 Scaled(ImVec2 value, float scale) {
  return {value.x * scale, value.y * scale};
}

void ScaleImPlotStyle(ImPlotStyle &style, float scale) {
  style.PlotDefaultSize = Scaled(style.PlotDefaultSize, scale);
  style.PlotMinSize = Scaled(style.PlotMinSize, scale);
  style.PlotBorderSize *= scale;
  style.MajorTickLen = Scaled(style.MajorTickLen, scale);
  style.MinorTickLen = Scaled(style.MinorTickLen, scale);
  style.MajorTickSize = Scaled(style.MajorTickSize, scale);
  style.MinorTickSize = Scaled(style.MinorTickSize, scale);
  style.MajorGridSize = Scaled(style.MajorGridSize, scale);
  style.MinorGridSize = Scaled(style.MinorGridSize, scale);
  style.PlotPadding = Scaled(style.PlotPadding, scale);
  style.LabelPadding = Scaled(style.LabelPadding, scale);
  style.LegendPadding = Scaled(style.LegendPadding, scale);
  style.LegendInnerPadding = Scaled(style.LegendInnerPadding, scale);
  style.LegendSpacing = Scaled(style.LegendSpacing, scale);
  style.MousePosPadding = Scaled(style.MousePosPadding, scale);
  style.AnnotationPadding = Scaled(style.AnnotationPadding, scale);
  style.DigitalPadding *= scale;
  style.DigitalSpacing *= scale;
}
} // namespace

namespace gui {
// public
GUI::GUI(GUIConfig config)
    : editorLayoutManager_(
          EditorLayoutManager::GetDefaultEditorConfigDirectory(),
          &editorLayoutBackend_),
      config_(std::move(config)) {}

GUI::~GUI() { Exit(); }

bool GUI::Start() {
  if (initialized_) {
    return true;
  }

  if (glfwInit() == GLFW_FALSE) {
    std::cerr << "Failed to initialize GLFW\n";
    return false;
  }
  glfwInitialized_ = true;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  window_ = glfwCreateWindow(config_.windowWidth,
      config_.windowHeight,
      config_.windowTitle.c_str(),
      nullptr,
      nullptr);

  if (window_ == nullptr) {
    std::cerr << "Failed to create GLFW window\n";
    Exit();
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(SwapInterval);

  IMGUI_CHECKVERSION();

  ImGui::CreateContext();
  ImPlot::CreateContext();
  imguiContextCreated_ = true;

  ImGuiIO &io = ImGui::GetIO();

  if (!windowStateSettings_.Register(windows_)) {
    std::cerr << "Window visibility settings are unavailable\n";
  }

  if (editorLayoutManager_.Initialize()) {
    workspaceIniPathString_ =
        editorLayoutManager_.GetWorkspaceIniPath().string();
    io.IniFilename = workspaceIniPathString_.c_str();
  } else {
    std::cerr << "Editor layouts are unavailable: "
              << editorLayoutManager_.GetLastError() << '\n';
  }

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  // DPI changes affect raster density through the GLFW/OpenGL backends. Keep
  // logical font sizing tied only to the responsive window-resolution scale.
  io.ConfigDpiScaleFonts = false;

  FlightUI::ApplyDarkEditorTheme();
  FlightUI::LoadPrimaryUIFont();
  baseImGuiStyle_ = ImGui::GetStyle();
  baseImPlotStyle_ = ImPlot::GetStyle();
  UpdateUIScale(true);

  if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
    std::cerr << "Failed to initialize ImGui GLFW backend\n";
    Exit();
    return false;
  }
  glfwBackendInitialized_ = true;

  if (!ImGui_ImplOpenGL3_Init(GlslVersion)) {
    std::cerr << "Failed to initialize ImGui OpenGL backend\n";
    Exit();
    return false;
  }
  openGlBackendInitialized_ = true;

  if (!editorIcons_.Initialize()) {
    std::cerr
        << "Editor icons are unavailable; using text-only window titles\n";
  }

  initialized_ = true;
  StartComponents();
  return true;
}

void GUI::Tick() {
  if (!initialized_) {
    return;
  }

  if (simulationMessageClient_ != nullptr) {
    simulationSnapshot_ = simulationMessageClient_->GetSimulationSnapshot();
    if (monitorController_ != nullptr) {
      monitorController_->SetInput({
          .primary = simulationMessageClient_->GetTelemetrySnapshot(
              sim::SimulationSlot::Primary),
          .baseline = simulationMessageClient_->GetTelemetrySnapshot(
              sim::SimulationSlot::Baseline),
          .dynamicModes =
              {
                  .history = simulationSnapshot_.linearization
                      .dynamicModeHistory.GetSnapshots(),
                  .errorMessage =
                      simulationSnapshot_.linearization.errorMessage,
                  .available = simulationSnapshot_.linearization.available,
                  .automaticUpdatesEnabled =
                      simulationSnapshot_.linearization.automaticUpdatesEnabled,
                  .updateInProgress =
                      simulationSnapshot_.linearization.updateInProgress,
              },
      });
    }
  }
  BeginFrame();
  RenderFrame();
  EndFrame();
}

void GUI::Exit() {
  editorIcons_.Shutdown();

  if (openGlBackendInitialized_) {
    ImGui_ImplOpenGL3_Shutdown();
    openGlBackendInitialized_ = false;
  }

  if (glfwBackendInitialized_) {
    ImGui_ImplGlfw_Shutdown();
    glfwBackendInitialized_ = false;
  }

  if (imguiContextCreated_) {
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    imguiContextCreated_ = false;
  }

  initialized_ = false;

  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }

  if (glfwInitialized_) {
    glfwTerminate();
    glfwInitialized_ = false;
  }
}

void GUI::RegisterComponent(std::unique_ptr<Component> component) {
  if (component == nullptr) {
    return;
  }

  components_.push_back(std::move(component));
  if (initialized_) {
    components_.back()->StartIfNeeded();
  }
}

void GUI::RegisterWindow(std::unique_ptr<Window> window) {
  if (window == nullptr) {
    return;
  }

  windows_.push_back(window.get());
  RegisterComponent(std::move(window));
}

bool GUI::ShouldClose() const {
  return window_ == nullptr || glfwWindowShouldClose(window_);
}

void GUI::RequestClose() {
  if (window_ != nullptr) {
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
  }
}

void GUI::SetSimulationMessageClient(
    application::SimulationMessageClient *client) {
  simulationMessageClient_ = client;
  RegisterFeatureTree();
}

void GUI::RegisterFeatureTree() {
  if (featureTreeRegistered_ || simulationMessageClient_ == nullptr) {
    return;
  }

  simulationController_ =
      std::make_unique<SimulationController>(*simulationMessageClient_);
  scenarioController_ = std::make_unique<ScenarioController>(
      std::filesystem::path{},
      architecture::EventSink<ScenarioLaunchRequested>{
          [this](const ScenarioLaunchRequested &event) {
            const bool succeeded = simulationController_->Handle(event);
            scenarioController_->Handle(ScenarioApplyCompleted{
                .succeeded = succeeded,
                .error = simulationController_->GetLastCommandError().value_or(
                    std::string{}),
            });
          }});
  gncController_ = std::make_unique<GNCController>(*simulationMessageClient_);
  linearizationController_ =
      std::make_unique<LinearizationController>(*simulationMessageClient_);
  monitorController_ = std::make_unique<MonitorController>(
      architecture::EventSink<MonitorAutomaticLinearizationChanged>{
          [this](const MonitorAutomaticLinearizationChanged &event) {
            linearizationController_->Handle(
                AutomaticLinearizationChanged{event.enabled});
          }});
  editorPlatformController_ =
      std::make_unique<EditorPlatformController>(editorLayoutManager_,
          fileDialogService_,
          [this] { ResetEditorLayoutToDefault(); });

  RegisterWindow<SimulationWindow>(*simulationController_);
  RegisterWindow<ScenarioWindow>();
  RegisterWindow<GNCWindow>(*gncController_);
  RegisterWindow<LinearizationWindow>(*linearizationController_);
  RegisterWindow<FlightDataMonitorWindow>(*monitorController_);
  primaryFlightVizWindow_ =
      &RegisterWindow<FlightVizWindow>(sim::SimulationSlot::Primary,
          &editorIcons_);
  baselineFlightVizWindow_ =
      &RegisterWindow<FlightVizWindow>(sim::SimulationSlot::Baseline,
          &editorIcons_);
  RegisterWindow<EditorIconBrowserWindow>(editorIcons_);
  RegisterComponent<SimulationControlWindow>(*simulationController_,
      *scenarioController_,
      *editorPlatformController_,
      editorIcons_);
  featureTreeRegistered_ = true;
}

void GUI::ResetEditorLayoutToDefault() {
  if (!imguiContextCreated_) {
    return;
  }
  ImGui::ClearIniSettings();
  const ImGuiID dockSpaceId = ImGui::GetID("DockSpace");
  ImGui::DockBuilderRemoveNode(dockSpaceId);
  editorLayoutManager_.ClearActivePreset();
  defaultDockLayoutInitialized_ = false;
}

void GUI::BeginFrame() {
  if (!initialized_) {
    return;
  }

  glfwPollEvents();
  UpdateUIScale();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void GUI::RenderFrame() {
  if (!initialized_) {
    return;
  }

  RenderMainMenuBar();
  RenderDockSpace();
  TickComponents();
}

void GUI::EndFrame() {
  if (!initialized_) {
    return;
  }

  ImGui::Render();

  int displayWidth = 0;
  int displayHeight = 0;
  glfwGetFramebufferSize(window_, &displayWidth, &displayHeight);

  const ImVec4 clearColor = FlightUI::GetDarkEditorApplicationBackground();
  glViewport(0, 0, displayWidth, displayHeight);
  glClearColor(clearColor.x * clearColor.w,
      clearColor.y * clearColor.w,
      clearColor.z * clearColor.w,
      clearColor.w);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(window_);

  if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
    GLFWwindow *currentContext = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    glfwMakeContextCurrent(currentContext);
  }
}

// private
void GUI::UpdateUIScale(bool force) {
  int windowWidth = 0;
  int windowHeight = 0;
  glfwGetWindowSize(window_, &windowWidth, &windowHeight);
  if (windowWidth <= 0 || windowHeight <= 0) {
    return;
  }

  const float uiScale =
      FlightUI::CalculateUIScale(static_cast<float>(windowWidth),
          static_cast<float>(windowHeight));
  if (!force && std::abs(uiScale - appliedUIScale_) < UIScaleChangeThreshold) {
    return;
  }

  appliedUIScale_ = uiScale;
  FlightUI::SetUIScale(uiScale);

  ImGuiStyle scaledImGuiStyle = baseImGuiStyle_;
  scaledImGuiStyle.ScaleAllSizes(uiScale);
  // The current ImGui backend rasterizes dynamically requested font sizes,
  // while framebuffer density remains a separate backend concern.
  scaledImGuiStyle.FontScaleMain =
      baseImGuiStyle_.FontScaleMain * FlightUI::CalculateUIFontScale(uiScale);
  ImGui::GetStyle() = scaledImGuiStyle;

  ImPlotStyle scaledImPlotStyle = baseImPlotStyle_;
  ScaleImPlotStyle(scaledImPlotStyle, uiScale);
  ImPlot::GetStyle() = scaledImPlotStyle;
}

void GUI::RenderDockSpace() {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float toolbarHeight = SimulationControlWindow::GetReservedHeight();
  const ImVec2 dockSpacePosition{
      viewport->WorkPos.x,
      viewport->WorkPos.y + toolbarHeight,
  };
  const ImVec2 dockSpaceSize{
      viewport->WorkSize.x,
      std::max(viewport->WorkSize.y - toolbarHeight, 1.0F),
  };

  ImGui::SetNextWindowPos(dockSpacePosition);
  ImGui::SetNextWindowSize(dockSpaceSize);
  ImGui::SetNextWindowViewport(viewport->ID);

  constexpr ImGuiWindowFlags HostFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
      | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
      | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus
      | ImGuiWindowFlags_NoNavFocus;

  char hostWindowLabel[32]{};
  std::snprintf(hostWindowLabel,
      sizeof(hostWindowLabel),
      "WindowOverViewport_%08X",
      viewport->ID);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
  ImGui::Begin(hostWindowLabel, nullptr, HostFlags);
  ImGui::PopStyleVar(3);

  const ImGuiID dockSpaceId = ImGui::GetID("DockSpace");
  InitializeDefaultDockLayout(dockSpaceId, dockSpaceSize);
  ImGui::DockSpace(dockSpaceId);
  ImGui::End();
}

void GUI::InitializeDefaultDockLayout(ImGuiID dockSpaceId,
    ImVec2 dockSpaceSize) {
  if (defaultDockLayoutInitialized_) {
    return;
  }
  defaultDockLayoutInitialized_ = true;

  const auto dockFlightVizWindows = [this](ImGuiID primaryNode,
                                        ImGuiID baselineNode) {
    ImGui::DockBuilderDockWindow(
        primaryFlightVizWindow_->GetWindowLabel().c_str(),
        primaryNode);
    ImGui::DockBuilderDockWindow(
        baselineFlightVizWindow_->GetWindowLabel().c_str(),
        baselineNode);
  };

  if (ImGui::DockBuilderGetNode(dockSpaceId) != nullptr) {
    const ImGuiID primaryWindowId =
        ImHashStr(primaryFlightVizWindow_->GetWindowLabel().c_str());
    const ImGuiID baselineWindowId =
        ImHashStr(baselineFlightVizWindow_->GetWindowLabel().c_str());
    if (ImGui::FindWindowSettingsByID(primaryWindowId) != nullptr
        || ImGui::FindWindowSettingsByID(baselineWindowId) != nullptr) {
      return;
    }

    ImGuiID targetNode = 0;
    if (const ImGuiWindowSettings *legacySettings =
            ImGui::FindWindowSettingsByID(ImHashStr("Flight Viz"));
        legacySettings != nullptr
        && ImGui::DockBuilderGetNode(legacySettings->DockId) != nullptr) {
      targetNode = legacySettings->DockId;
    } else if (const ImGuiDockNode *centralNode =
                   ImGui::DockBuilderGetCentralNode(dockSpaceId)) {
      targetNode = centralNode->ID;
    }

    if (targetNode != 0) {
      ImGuiID baselineNode = 0;
      ImGuiID primaryNode = 0;
      ImGui::DockBuilderSplitNode(targetNode,
          ImGuiDir_Down,
          0.5F,
          &baselineNode,
          &primaryNode);
      dockFlightVizWindows(primaryNode, baselineNode);
      ImGui::DockBuilderFinish(dockSpaceId);
    }
    return;
  }

  ImGui::DockBuilderRemoveNode(dockSpaceId);
  ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockSpaceId, dockSpaceSize);

  ImGuiID rightNode = 0;
  ImGuiID leftAndCenterNode = 0;
  ImGui::DockBuilderSplitNode(dockSpaceId,
      ImGuiDir_Right,
      0.30F,
      &rightNode,
      &leftAndCenterNode);
  ImGuiID leftNode = 0;
  ImGuiID centerNode = 0;
  ImGui::DockBuilderSplitNode(leftAndCenterNode,
      ImGuiDir_Left,
      0.34F,
      &leftNode,
      &centerNode);
  ImGuiID baselineNode = 0;
  ImGuiID primaryNode = 0;
  ImGui::DockBuilderSplitNode(centerNode,
      ImGuiDir_Down,
      0.5F,
      &baselineNode,
      &primaryNode);

  dockFlightVizWindows(primaryNode, baselineNode);
  ImGui::DockBuilderDockWindow("GNC", leftNode);
  ImGui::DockBuilderDockWindow("Monitor", leftNode);
  ImGui::DockBuilderDockWindow("FG Linearization", leftNode);
  ImGui::DockBuilderDockWindow("Current Scenario###Scenario", rightNode);
  ImGui::DockBuilderDockWindow("Simulation", rightNode);
  ImGui::DockBuilderFinish(dockSpaceId);
}

void GUI::RenderMainMenuBar() {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }

  RenderSimulationMenu();
  RenderWindowMenu();

  ImGui::EndMainMenuBar();
}

void GUI::RenderSimulationMenu() {
  if (!ImGui::BeginMenu("Simulation")) {
    return;
  }

  const SimulationTransportProps props =
      simulationController_->GetTransportProps();
  const sim::SimulationExecutionState executionState = props.executionState;
  const bool scenarioInactive = !props.scenarioStatus.has_value();

  ImGui::BeginDisabled(
      executionState != sim::SimulationExecutionState::Running);
  if (ImGui::MenuItem("Pause")) {
    simulationController_->Handle(SimulationPauseRequested{});
  }
  ImGui::EndDisabled();

  ImGui::BeginDisabled(executionState != sim::SimulationExecutionState::Paused);
  if (ImGui::MenuItem("Resume")) {
    simulationController_->Handle(SimulationResumeRequested{});
  }
  if (ImGui::MenuItem("Tick Once")) {
    simulationController_->Handle(SimulationStepRequested{});
  }
  ImGui::EndDisabled();

  ImGui::BeginDisabled(
      !scenarioInactive
      || executionState == sim::SimulationExecutionState::Stopped);
  if (ImGui::MenuItem("Reset")) {
    simulationController_->Handle(SimulationResetRequested{});
  }
  ImGui::EndDisabled();

  ImGui::Separator();

  if (ImGui::MenuItem("Exit")) {
    RequestClose();
  }

  ImGui::EndMenu();
}

void GUI::RenderWindowMenu() {
  if (!ImGui::BeginMenu("Window")) {
    return;
  }

  for (Window *window : windows_) {
    if (ImGui::MenuItem(window->GetTitle().c_str(),
            nullptr,
            window->GetVisiblePtr())) {
      ImGui::MarkIniSettingsDirty();
    }
  }

  ImGui::Separator();

  if (ImGui::MenuItem("Show All")) {
    for (Window *window : windows_) {
      window->SetVisible(true);
    }
    ImGui::MarkIniSettingsDirty();
  }

  if (ImGui::MenuItem("Hide All")) {
    for (Window *window : windows_) {
      window->SetVisible(false);
    }
    ImGui::MarkIniSettingsDirty();
  }

  ImGui::EndMenu();
}

void GUI::StartComponents() {
  for (const auto &component : components_) {
    component->StartIfNeeded();
  }
}

void GUI::TickComponents() {
  const GUIFrameContext context{simulationSnapshot_, editorIcons_};
  for (const auto &component : components_) {
    component->Tick(context);
  }
}
} // namespace gui
