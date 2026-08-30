add_library(jsb_gui_architecture INTERFACE)
target_compile_features(jsb_gui_architecture INTERFACE cxx_std_20)
target_include_directories(jsb_gui_architecture INTERFACE ${PROJECT_SOURCE_DIR}/src)

add_library(jsb_monitor STATIC
  features/monitor/MonitorController.cpp
)
target_compile_features(jsb_monitor PUBLIC cxx_std_20)
set_target_properties(jsb_monitor PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_include_directories(jsb_monitor PUBLIC ${PROJECT_SOURCE_DIR}/src)

if(JSB_BUILD_EDITOR)
  add_library(jsb_editor STATIC
    features/flightviz/FlightVisualizer.cpp
    GUI.cpp
    Window.cpp
    resources/EditorIconRegistry.cpp
    layout/EditorLayoutFileSerializer.cpp
    layout/EditorLayoutManager.cpp
    layout/EditorWindowStateSettings.cpp
    layout/Toolbar.cpp
    features/autopilot_selector/AutopilotSelectorController.cpp
    features/autopilot_selector/AutopilotSelectorView.cpp
    features/editor/EditorPlatformController.cpp
    features/gnc/GNCController.cpp
    features/linearization/LinearizationController.cpp
    features/monitor/MonitorView.cpp
    features/simulation/ScenarioController.cpp
    features/simulation/ScenarioSetupPopup.cpp
    features/simulation/SimulationController.cpp
    platform/FileDialogService.cpp
    panels/AutopilotPanel.cpp
    panels/BaselineAutopilotPanel.cpp
    panels/ManualControlPanel.cpp
    panels/TrimPanel.cpp
    windows/SimulationControlWindow.cpp
    windows/SimulationWindow.cpp
    windows/EditorIconBrowserWindow.cpp
    windows/ScenarioWindow.cpp
    windows/FlightConsoleWindow.cpp
    windows/monitor/FlightDataMonitorWindow.cpp
    windows/viz/FlightVizWindow.cpp
    windows/GNCWindow.cpp
    windows/LinearizationValueTransform.cpp
    windows/LinearizationWindow.cpp
    windows/samples/DeclarativeDemoWindow.cpp
    windows/samples/SamplePlotWindow.cpp
  )
  target_compile_features(jsb_editor PUBLIC cxx_std_20)
  target_precompile_headers(jsb_editor PRIVATE
    <memory>
    <optional>
    <string>
    <utility>
    <vector>
  )
  set_target_properties(jsb_editor PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  if(JSB_ENABLE_UNITY_BUILD)
    set_target_properties(jsb_editor PROPERTIES
      UNITY_BUILD ON
      UNITY_BUILD_BATCH_SIZE 4
    )
    set_source_files_properties(
      resources/EditorIconRegistry.cpp
      PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
  endif()
  target_include_directories(jsb_editor
    PUBLIC ${PROJECT_SOURCE_DIR}/src
    PRIVATE ${stb_SOURCE_DIR}
  )
  target_compile_definitions(jsb_editor PRIVATE
    JSB_UNITY_EDITOR_ICON_DIRECTORY="${PROJECT_SOURCE_DIR}/assets/third_party/unity-editor-icons/icons/small"
  )
  target_link_libraries(jsb_editor
    PUBLIC
      jsb_gui_architecture
      jsb_monitor
      jsb_messaging
      flightui
    PRIVATE yaml-cpp::yaml-cpp
  )
  if(WIN32)
    target_link_libraries(jsb_editor PUBLIC comdlg32 shell32 ws2_32)
  endif()
endif()
