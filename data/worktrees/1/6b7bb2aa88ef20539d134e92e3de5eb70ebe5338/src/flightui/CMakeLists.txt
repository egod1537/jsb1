add_library(flightui STATIC
  core/UICommon.cpp
  core/Theme.cpp
  core/UIFont.cpp
  core/UIScale.cpp
  core/UIElement.cpp
  layout/Window.cpp
  layout/Panel.cpp
  layout/FoldOut.cpp
  layout/ToggleFoldOut.cpp
  layout/KeyValueGrid.cpp
  layout/PropertyTable.cpp
  layout/Toolbar.cpp
  layout/TabGroup.cpp
  layout/VerticalLayout.cpp
  layout/HorizontalLayout.cpp
  controls/Text.cpp
  controls/Heading.cpp
  controls/Latex.cpp
  controls/ValueLabel.cpp
  controls/Button.cpp
  controls/Combo.cpp
  controls/Toggle.cpp
  controls/Input.cpp
  controls/Slider.cpp
  controls/ScalarEditor.cpp
  controls/IconButton.cpp
  controls/StatusBadge.cpp
  controls/Separator.cpp
  controls/Space.cpp
  controls/Custom.cpp
  latex/LatexRenderer.cpp
  plot/DataView.cpp
  plot/Plot.cpp
  visualization/core/Component.cpp
  visualization/core/FlightPathHistory.cpp
  visualization/core/GameObject.cpp
  visualization/core/Scene.cpp
  visualization/render/CameraComponent.cpp
  visualization/render/LineCanvas.cpp
  visualization/components/GroundGridRenderer.cpp
  visualization/components/AltitudeCueRenderer.cpp
  visualization/components/FlightCameraController.cpp
  visualization/components/AircraftWireframeRenderer.cpp
  visualization/components/TelemetryOverlay.cpp
)

target_compile_features(flightui PUBLIC
  cxx_std_20
)

set_target_properties(flightui PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

target_include_directories(flightui PUBLIC
  ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(flightui PUBLIC
  jsb_common
  imgui
  implot
)

target_compile_definitions(flightui PRIVATE
  JSB_INTER_FONT_FILE="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)
