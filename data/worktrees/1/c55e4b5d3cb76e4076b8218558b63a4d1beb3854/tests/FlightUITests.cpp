#include "flightui/FlightUI.hpp"

#include <cassert>
#include <cmath>
#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace UI = FlightUI;

template <typename T>
concept CanSliderDouble =
    requires { UI::SliderDouble("Value", std::declval<T>(), 0.0, 1.0); };

template <typename T>
concept CanSliderFloat =
    requires { UI::SliderFloat("Value", std::declval<T>(), 0.0F, 1.0F); };

template <typename T>
concept CanSliderInt =
    requires { UI::SliderInt("Value", std::declval<T>(), 0, 10); };

template <typename T>
concept CanToggle = requires { UI::Toggle("Enabled", std::declval<T>()); };

template <typename T>
concept CanMakeDataView = requires { UI::DataView::From(std::declval<T>()); };

template <typename T>
concept CanMakeRingBufferDataView =
    requires(const T &buffer) { buffer.data_view(); };

static_assert(CanSliderDouble<double>);
static_assert(CanSliderDouble<const double &>);
static_assert(CanSliderDouble<double &&>);
static_assert(CanSliderFloat<float>);
static_assert(CanSliderFloat<const float &>);
static_assert(CanSliderFloat<float &&>);
static_assert(CanSliderInt<int>);
static_assert(CanSliderInt<const int &>);
static_assert(CanSliderInt<int &&>);
static_assert(CanToggle<bool>);
static_assert(CanToggle<const bool &>);
static_assert(CanToggle<bool &&>);
static_assert(CanMakeDataView<const std::vector<double> &>);
static_assert(!CanMakeDataView<std::vector<double> &&>);
static_assert(CanMakeDataView<const std::vector<float> &>);
static_assert(!CanMakeDataView<std::vector<float> &&>);
static_assert(CanMakeRingBufferDataView<ds::RingBuffer<double>>);
static_assert(CanMakeRingBufferDataView<ds::RingBuffer<float>>);
static_assert(!CanMakeRingBufferDataView<ds::RingBuffer<int>>);
static_assert(requires {
  UI::Button("Reset")
      .OnAction([] {})
      .Width(80.0F)
      .Height(24.0F)
      .Enabled(true)
      .Tooltip("Reset controls")
      .Id("reset-button");
  UI::Button("Typo Alias").Widht(80.0F);
});
static_assert(requires {
  UI::Toggle("Enabled", true).OnChanged([](bool) {});
  UI::SliderFloat("Value", 0.5F, 0.0F, 1.0F).OnChanged([](float) {});
  UI::SliderDouble("Value", 0.5, 0.0, 1.0).OnChanged([](double) {});
  UI::SliderInt("Value", 5, 0, 10).OnChanged([](int) {});
  UI::SliderDouble("Value", 0.5, 0.0, 1.0).FillAvailableWidth(96.0F);
  UI::ScalarEditor("Gain", 0.5)
      .Range(0.0, 1.0)
      .Step(0.01)
      .FastStep(0.1)
      .Format("%.2f")
      .ShowSlider()
      .ShowInput()
      .ShowStepper()
      .Enabled(true)
      .Tooltip("Gain")
      .OnChanged([](double) {});
  UI::PropertyTable("Properties")
      .LabelWidth(112.0F)
      .ColumnSpacing(4.0F)
      .RowPadding(2.0F)
      .AlternatingRows()
      .Enabled(true)
      .Visible(true)
      .Tooltip("Property values")
      .Add("Gain", UI::Text("0.50"));
  UI::PropertyGrid("Responsive Properties")
      .LabelWidthRatio(0.4F)
      .MinimumLabelWidth(100.0F)
      .MaximumLabelWidth(180.0F)
      .SingleColumnThreshold(320.0F)
      .AlternatingRows()
      .Add(UI::PropertyRow("Gain").Tooltip("Gain value")[UI::Text("0.50")]);
  UI::Toolbar()
      .Id("Tools")
      .Compact()
      .Height(28.0F)
      .Left(UI::Text("Tools"))
      .Right(UI::Button("Action"));
  UI::IconButton("Icon", ImTextureID_Invalid)
      .FallbackText("I")
      .Size(22.0F)
      .Selected()
      .Enabled(true)
      .Tooltip("Icon action")
      .OnAction([] {});
  UI::ToggleIconButton("ToggleIcon", ImTextureID_Invalid, true)
      .OnChanged([](bool) {});
  UI::StatusBadge("Ready", UI::StatusTone::Success);
});
static_assert(requires(bool isOpen) {
  UI::FoldOut("Advanced")
      .Open(isOpen)
      .DefaultOpen()
      .Flags(ImGuiTreeNodeFlags_Framed)
      .HeaderLeft(UI::Toggle("##Selected", true), 18.0F)
      .HeaderRight(UI::Toggle("Enabled", true), 96.0F)
      .Enabled(true)
      .Visible(true)
      .Tooltip("Advanced settings")
      .Id("advanced")[UI::Text("Fold out content")];
  UI::TabGroup("Main Tabs")
      .Flags(ImGuiTabBarFlags_Reorderable)
      .Enabled(true)
      .Visible(true)
      .Tooltip("Main tabs")
      .Id("main-tabs")[+UI::Tab("Controls")
              .Open(isOpen)
              .Flags(ImGuiTabItemFlags_SetSelected)
              .Enabled(true)
              .Visible(true)
              .Tooltip("Controls")
              .Id("controls")[UI::Text("Controls")]];
  UI::ToggleFoldOut("Enabled Section", true)
      .Open(isOpen)
      .DefaultOpen()
      .Section()
      .ToggleEnabled(true)
      .Visible(true)
      .Tooltip("Section")
      .Id("enabled-section")
      .OnChanged([](bool) {})[UI::Text("Content")];
});

int main() {
  constexpr float ScaleTolerance = 0.0001F;
  constexpr double RangeTolerance = 1.0e-9;

  assert(UI::ResolvePropertyGridLayout(319.0F, 320.0F)
         == UI::PropertyGridLayout::SingleColumn);
  assert(UI::ResolvePropertyGridLayout(320.0F, 320.0F)
         == UI::PropertyGridLayout::TwoColumns);
  assert(!UI::IsAlternatePropertyRow(0));
  assert(UI::IsAlternatePropertyRow(1));
  assert(UI::NormalizeScalarEditorValue(0.5, 0.0, 1.0) == 0.5);
  assert(UI::NormalizeScalarEditorValue(-1.0, 0.0, 1.0) == 0.0);
  assert(UI::NormalizeScalarEditorValue(2.0, 0.0, 1.0) == 1.0);
  assert(!UI::NormalizeScalarEditorValue(
              std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0)
              .has_value());

  const auto constantPositiveRange = UI::ExpandYAxisRange(2.0, 2.0);
  assert(constantPositiveRange.has_value());
  assert(std::abs(constantPositiveRange->Min - 1.9) < RangeTolerance);
  assert(std::abs(constantPositiveRange->Max - 2.1) < RangeTolerance);

  const auto constantZeroRange = UI::ExpandYAxisRange(0.0, 0.0);
  assert(constantZeroRange.has_value());
  assert(std::abs(constantZeroRange->Min + 0.1) < RangeTolerance);
  assert(std::abs(constantZeroRange->Max - 0.1) < RangeTolerance);

  const auto constantNegativeRange = UI::ExpandYAxisRange(-3.0, -3.0);
  assert(constantNegativeRange.has_value());
  assert(std::abs(constantNegativeRange->Min + 3.15) < RangeTolerance);
  assert(std::abs(constantNegativeRange->Max + 2.85) < RangeTolerance);

  const auto nearlyConstantRange = UI::ExpandYAxisRange(1.999999, 2.000001);
  assert(nearlyConstantRange.has_value());
  assert(nearlyConstantRange->Min < 1.9);
  assert(nearlyConstantRange->Max > 2.1);

  const auto normalRange = UI::ExpandYAxisRange(-10.0, 10.0);
  assert(normalRange.has_value());
  assert(std::abs(normalRange->Min + 12.0) < RangeTolerance);
  assert(std::abs(normalRange->Max - 12.0) < RangeTolerance);

  const double nanValue = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  assert(!UI::ExpandYAxisRange(nanValue, 1.0).has_value());
  assert(!UI::ExpandYAxisRange(0.0, infinity).has_value());

  assert(
      std::abs(UI::CalculateUIScale(1280.0F, 720.0F) - 1.0F) < ScaleTolerance);
  assert(
      std::abs(UI::CalculateUIScale(1024.0F, 768.0F) - 0.8F) < ScaleTolerance);
  assert(std::abs(UI::CalculateUIScale(640.0F, 360.0F) - UI::MinimumUIScale)
         < ScaleTolerance);
  assert(std::abs(UI::CalculateUIScale(3840.0F, 2160.0F) - UI::MaximumUIScale)
         < ScaleTolerance);

  UI::SetUIScale(UI::CalculateUIScale(1920.0F, 1080.0F));
  assert(std::abs(UI::Ui(100.0F) - 150.0F) < ScaleTolerance);
  UI::SetUIScale(UI::CalculateUIScale(1024.0F, 768.0F));
  assert(std::abs(UI::Ui(100.0F) - 80.0F) < ScaleTolerance);
  const UI::Vector2 scaledPlotSize = UI::UiSize({-1.0F, 245.0F});
  assert(scaledPlotSize.X == -1.0F);
  assert(std::abs(scaledPlotSize.Y - 196.0F) < ScaleTolerance);
  UI::SetUIScale(UI::CalculateUIScale(1920.0F, 1080.0F));
  assert(std::abs(UI::Ui(100.0F) - 150.0F) < ScaleTolerance);
  UI::SetUIScale(1.0F);

  std::vector<double> xValues{0.0, 1.0, 2.0};
  std::vector<double> yValues{0.0, 1.0, 4.0};
  const UI::DataView xView = UI::DataView::From(xValues);
  ds::RingBuffer<double> ringValues(3);

  ringValues.push_back(1.0);
  ringValues.push_back(2.0);
  ringValues.push_back(3.0);
  ringValues.push_back(4.0);

  assert(xView.GetData() == xValues.data());
  assert(xView.GetCount() == xValues.size());
  assert(xView.GetStride() == sizeof(double));
  assert(xView.GetType() == UI::DataType::Double);
  struct StridedPoint {
    double x;
    double y;
  };
  const std::vector<StridedPoint> stridedPoints{{0.0, 1.0}, {2.0, 3.0}};
  const UI::DataView stridedView(&stridedPoints[0].x,
      stridedPoints.size(),
      sizeof(StridedPoint));
  assert(stridedView.GetStride() == sizeof(StridedPoint));
  assert(ringValues.capacity() == 3);
  assert(ringValues.size() == 3);
  assert(ringValues.offset() == 1);
  assert(ringValues[0] == 2.0);
  assert(ringValues[1] == 3.0);
  assert(ringValues[2] == 4.0);
  assert(ringValues.to_vector() == std::vector<double>({2.0, 3.0, 4.0}));

  const UI::DataView ringView = ringValues.data_view();
  assert(ringView.GetData() == ringValues.data());
  assert(ringView.GetCount() == ringValues.size());
  assert(ringView.GetType() == UI::DataType::Double);

  UI::UIElement text = UI::Text(std::string("Temporary text"));
  assert(text.IsValid());
  UI::UIElement temporaryLatex =
      UI::Latex(std::string(R"(\dot{x} = Ax + Bu)"));
  assert(temporaryLatex.IsValid());
  UI::UIElement scaledLatex = UI::Latex(
      R"(\frac{\partial f}{\partial x})", {.Scale = 1.25F});
  assert(scaledLatex.IsValid());

  ImGui::CreateContext();
  ImPlot::CreateContext();
  UI::ApplyDarkEditorTheme();
  assert(UI::LoadPrimaryUIFont());
  assert(UI::GetPrimaryUIFontPath().filename() == "Inter-Regular.ttf");

  const ImGuiStyle &darkEditorStyle = ImGui::GetStyle();
  const ImPlotStyle &darkEditorPlotStyle = ImPlot::GetStyle();
  assert(std::abs(darkEditorStyle.Colors[ImGuiCol_WindowBg].x - 30.0F / 255.0F)
         < ScaleTolerance);
  assert(
      std::abs(darkEditorStyle.Colors[ImGuiCol_CheckMark].z - 255.0F / 255.0F)
      < ScaleTolerance);
  assert(darkEditorStyle.WindowRounding == 5.0F);
  assert(darkEditorStyle.FrameRounding == 4.0F);
  assert(darkEditorStyle.FrameBorderSize == 0.0F);
  assert(darkEditorStyle.WindowPadding.x == 10.0F);
  assert(darkEditorStyle.FramePadding.y == 5.0F);
  assert(darkEditorStyle.Colors[ImGuiCol_Button].x
         < darkEditorStyle.Colors[ImGuiCol_ButtonHovered].x);
  assert(darkEditorStyle.Colors[ImGuiCol_TabSelected].x
         > darkEditorStyle.Colors[ImGuiCol_Tab].x);
  const ImVec4 propertyRowBackground =
      UI::GetThemeColor(UI::ThemeColor::PropertyRowBackground);
  const ImVec4 propertyRowBackgroundAlternate =
      UI::GetThemeColor(UI::ThemeColor::PropertyRowBackgroundAlternate);
  assert(propertyRowBackground.w == 0.0F);
  assert(propertyRowBackgroundAlternate.w > propertyRowBackground.w);
  assert(propertyRowBackgroundAlternate.w < 0.3F);
  const UI::StatusBadgeStyle neutralBadge =
      UI::GetStatusBadgeStyle(UI::StatusTone::Neutral);
  const UI::StatusBadgeStyle successBadge =
      UI::GetStatusBadgeStyle(UI::StatusTone::Success);
  const UI::StatusBadgeStyle errorBadge =
      UI::GetStatusBadgeStyle(UI::StatusTone::Error);
  assert(neutralBadge.Background.w > 0.0F);
  assert(successBadge.Text.y > successBadge.Text.x);
  assert(errorBadge.Text.x > errorBadge.Text.y);
  assert(darkEditorPlotStyle.Colors[ImPlotCol_PlotBg].x
         < darkEditorPlotStyle.Colors[ImPlotCol_FrameBg].x);
  assert(darkEditorPlotStyle.Colors[ImPlotCol_AxisGrid].w < 0.25F);
  assert(ImGui::GetIO().Fonts->Fonts.Size == 1);
  assert(ImGui::GetIO().FontDefault != nullptr);
  assert(std::abs(ImGui::GetIO().FontDefault->LegacySize - UI::BaseUIFontSize)
         < ScaleTolerance);
  assert(std::abs(UI::CalculateUIFontScale(1.0F) - 1.0F) < ScaleTolerance);
  assert(std::abs(UI::CalculateUIFontScale(0.7F) * UI::BaseUIFontSize
                  - UI::MinimumUIFontSize)
         < ScaleTolerance);
  const float largeFontScale = UI::CalculateUIFontScale(1.5F);
  const float smallFontScale = UI::CalculateUIFontScale(0.8F);
  const float restoredFontScale = UI::CalculateUIFontScale(1.5F);
  assert(std::abs(smallFontScale * UI::BaseUIFontSize - UI::MinimumUIFontSize)
         < ScaleTolerance);
  assert(std::abs(restoredFontScale - largeFontScale) < ScaleTolerance);
  assert(std::abs(UI::CalculateUIFontScale(1.5F) - 1.5F) < ScaleTolerance);

  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(800.0F, 600.0F);
  io.DeltaTime = 1.0F / 60.0F;
  unsigned char *fontPixels = nullptr;
  int fontWidth = 0;
  int fontHeight = 0;
  io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
  ImFontBaked *interRegular = io.FontDefault->GetFontBaked(UI::BaseUIFontSize);
  assert(interRegular->FindGlyphNoFallback('A') != nullptr);
  assert(interRegular->FindGlyphNoFallback('0') != nullptr);
  assert(interRegular->FindGlyphNoFallback('%') != nullptr);
  assert(interRegular->FindGlyphNoFallback('/') != nullptr);
  assert(interRegular->FindGlyphNoFallback(0x03B1) != nullptr);
  assert(interRegular->FindGlyphNoFallback(0x03B2) != nullptr);
  assert(interRegular->FindGlyphNoFallback(0x03C6) != nullptr);
  assert(interRegular->FindGlyphNoFallback(0x03B8) != nullptr);
  assert(interRegular->FindGlyphNoFallback(0x03C8) != nullptr);
  assert(interRegular->FindGlyphNoFallback(0x0307) != nullptr);

  bool isOpen = true;
  bool isTabOpen = true;
  bool isFoldOutOpen = true;
  bool enabled = false;
  double throttle = 0.5;
  double linkedXAxisMin = 0.0;
  double linkedXAxisMax = 2.0;
  int clicks = 0;
  std::vector<int> plotCallbackOrder;

  ImGui::NewFrame();

  ImGui::Begin("LaTeX FlightUI Test");
  temporaryLatex.Render();
  scaledLatex.Render();
  ImGui::End();

  UI::Window("FlightUI Test")
      .Open(isOpen)
      .InitialSize({640.0F, 480.0F})[UI::VerticalLayout({
          UI::Heading("Controls"),
          UI::Text(std::string("Temporary text")),
          UI::FoldOut("Fold Out")
              .Open(isFoldOutOpen)
              .DefaultOpen()
              .Flags(ImGuiTreeNodeFlags_Framed)[UI::Text("Fold out body")],
          UI::Panel("Panel").Border(true)[UI::VerticalLayout({
              UI::Toggle("Enabled", enabled).OnChanged([&enabled](bool value) {
                enabled = value;
              }),
              UI::SliderDouble("Throttle", throttle, 0.0, 1.0)
                  .OnChanged([&throttle](double value) { throttle = value; })
                  .Width(180.0F),
              UI::ValueLabel("Throttle readout", throttle + 0.125, "{:.2f}"),
              UI::Button("Reset")
                  .OnAction([&clicks] { ++clicks; })
                  .Width(80.0F),
              UI::PropertyTable("Test Properties")
                  .LabelWidth(112.0F)
                  .AlternatingRows()
                  .Add("Throttle",
                      UI::SliderDouble("##PropertyThrottle", throttle, 0.0, 1.0)
                          .FillAvailableWidth()),
              UI::StatusBadge("Ready", UI::StatusTone::Success),
              UI::Custom([] { ImGui::TextUnformatted("Custom"); }),
          })],
          UI::TabGroup("Telemetry Tabs")
              .Flags(ImGuiTabBarFlags_Reorderable)
                  [+UI::Tab("Controls")[UI::Text("Control tab")]
                      + UI::Tab("Monitor").Open(isTabOpen).Tooltip(
                          "Monitor tab")[UI::Text("Monitor tab")]],
          UI::Plot("Plot")
              .Offset(1)
              .Height(120.0F)
              .XAxisFlags(ImPlotAxisFlags_None)
              .YAxisFlags(ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit)
              .XAxisLinks(linkedXAxisMin, linkedXAxisMax)
              .XAxisTicks({0.0, 1.0, 2.0})
              .YAxisLimits(0.0, 4.0)
              .XAxisLabel("X")
              .YAxisLabel("Y")
              .AddLine("Line", xValues, yValues)
              .AddLine("Offset Line", xValues, yValues, 2)
              .AddLine("Ring Line", ringView, ringView, ringValues.offset())
              .AddScatter("Scatter", xValues, yValues, 1),
      })];

  // clang-format off
  UI::Window("Slate Style FlightUI Test")
  [
    +UI::Heading("Controls")
    + UI::Panel("Panel")
          .Border(true)
          [
            +UI::Toggle("Enabled", enabled)
                 .OnChanged([&enabled](bool value) { enabled = value; })
            + UI::SliderDouble("Throttle", throttle, 0.0, 1.0)
                  .OnChanged([&throttle](double value) { throttle = value; })
                  .Format("%.2f")
            + UI::Button("Reset").OnAction([&clicks] { ++clicks; })
          ]
    + UI::HorizontalLayout()
          .Spacing(8.0F)
          [
            +UI::Text("Left")
            + UI::Text("Right")
          ]
  ];
  // clang-format on

  UI::UIElement chainedLayout =
      UI::VerticalLayout() + UI::Text("First") + UI::Text("Second");
  assert(chainedLayout.IsValid());

  UI::Window("Second FlightUI Test")[UI::Text("Second window")];

  ImGui::Begin("Plot Callback Order Test");
  UI::UIElement callbackPlot =
      UI::Plot("Callback Order")
          .Height(120.0F)
          .Underlay([&plotCallbackOrder] { plotCallbackOrder.push_back(1); })
          .AddLine("Callback Line", xValues, yValues)
          .Overlay([&plotCallbackOrder] { plotCallbackOrder.push_back(2); });
  callbackPlot.Render();
  ImGui::End();

  ImGui::Render();

  assert(isOpen);
  assert(isTabOpen);
  assert(isFoldOutOpen);
  assert(clicks == 0);
  assert(plotCallbackOrder == std::vector<int>({1, 2}));
  assert(std::abs(linkedXAxisMin) < RangeTolerance);
  assert(std::abs(linkedXAxisMax - 2.0) < RangeTolerance);

  bool controllerOpen = true;
  bool controllerEnabled = false;
  ImVec2 controllerHeaderMinimum{};
  const auto renderControllerHeaderFrame = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420.0F, 180.0F), ImGuiCond_Always);
    ImGui::Begin("FoldOut Header Interaction Test",
        nullptr,
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    controllerHeaderMinimum = ImGui::GetCursorScreenPos();
    UI::ToggleFoldOut("Roll Hold", controllerEnabled)
        .Open(controllerOpen)
        .DefaultOpen()
        .Id("ControllerHeaderInteraction")
        .OnChanged([&controllerEnabled](bool enabledValue) {
          controllerEnabled = enabledValue;
        })[UI::Text("Controller settings")]
        .Render();
    ImGui::End();
    ImGui::Render();
  };

  io.AddMousePosEvent(-1000.0F, -1000.0F);
  renderControllerHeaderFrame();
  const ImVec2 toggleCenter{controllerHeaderMinimum.x
          + ImGui::GetTreeNodeToLabelSpacing()
          + ImGui::GetFrameHeight() * 0.5F,
      controllerHeaderMinimum.y + ImGui::GetFrameHeight() * 0.5F};
  io.AddMousePosEvent(toggleCenter.x, toggleCenter.y);
  renderControllerHeaderFrame();
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  renderControllerHeaderFrame();
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  renderControllerHeaderFrame();
  assert(controllerEnabled);
  assert(controllerOpen);

  const ImVec2 titlePosition{
      controllerHeaderMinimum.x + 48.0F, toggleCenter.y};
  io.AddMousePosEvent(titlePosition.x, titlePosition.y);
  renderControllerHeaderFrame();
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  renderControllerHeaderFrame();
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  renderControllerHeaderFrame();
  assert(controllerEnabled);
  assert(!controllerOpen);
  io.AddMousePosEvent(-1000.0F, -1000.0F);

  ImVec2 toolbarButtonMinimum{};
  ImVec2 toolbarButtonMaximum{};
  const auto renderToolbarFrame = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420.0F, 120.0F), ImGuiCond_Always);
    ImGui::Begin("Toolbar Alignment Test",
        nullptr,
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    UI::Toolbar()
        .Id("Alignment")
        .AlignRight()
        .Height(28.0F)[UI::Custom([&] {
          ImGui::Button("Action", ImVec2(54.0F, 0.0F));
          toolbarButtonMinimum = ImGui::GetItemRectMin();
          toolbarButtonMaximum = ImGui::GetItemRectMax();
        })]
        .Render();
    ImGui::End();
    ImGui::Render();
  };
  renderToolbarFrame();
  renderToolbarFrame();
  assert(toolbarButtonMinimum.x > 350.0F);
  assert(toolbarButtonMaximum.x < 440.0F);

  const std::vector<double> focusedNearValues{0.0, 1.0, 0.5};
  const std::vector<double> focusedFarValues{100.0, 200.0, 150.0};
  ImPlotRange focusedYAxisRange;
  const auto renderFocusedYAxisFrame = [&] {
    ImGui::NewFrame();
    ImGui::Begin("Focused Y Axis Test");
    UI::UIElement focusedPlot =
        UI::Plot("Hidden Series Range")
            .Height(120.0F)
            .FocusedYAxis()
            .XAxisLimitsAlways(0.0, 2.0)
            .AddLine("Near", xValues, focusedNearValues)
            .AddLine("Far", xValues, focusedFarValues)
            .Overlay([&focusedYAxisRange] {
              if (ImPlotItem *farItem = ImPlot::GetItem("Far")) {
                farItem->Show = false;
              }
              focusedYAxisRange = ImPlot::GetPlotLimits().Y;
            });
    focusedPlot.Render();
    ImGui::End();
    ImGui::Render();
  };

  renderFocusedYAxisFrame();
  renderFocusedYAxisFrame();
  renderFocusedYAxisFrame();
  assert(focusedYAxisRange.Min < 0.0);
  assert(focusedYAxisRange.Max > 1.0);
  assert(focusedYAxisRange.Max < 10.0);

  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  return 0;
}
