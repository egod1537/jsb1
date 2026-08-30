#pragma once

#include "flightui/core/UIElement.hpp"
#include "flightui/plot/DataView.hpp"

#include <implot.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace FlightUI {
struct PlotAxisRange {
  double Min;
  double Max;
};

std::optional<PlotAxisRange> ExpandYAxisRange(double minValue, double maxValue);

class PlotBuilder {
public:
  // Lifetime
  explicit PlotBuilder(std::string title);
  PlotBuilder(const PlotBuilder &other);
  PlotBuilder(PlotBuilder &&other) noexcept;
  PlotBuilder &operator=(const PlotBuilder &other);
  PlotBuilder &operator=(PlotBuilder &&other) noexcept;
  ~PlotBuilder();

  // Explicit configuration
  PlotBuilder &SetSize(Vector2 size);
  PlotBuilder &SetWidth(float width);
  PlotBuilder &SetHeight(float height);
  PlotBuilder &SetXAxisLabel(std::string label);
  PlotBuilder &SetYAxisLabel(std::string label);
  PlotBuilder &SetXAxisFlags(ImPlotAxisFlags flags);
  PlotBuilder &SetYAxisFlags(ImPlotAxisFlags flags);
  PlotBuilder &SetXAxisLimits(double min, double max,
      ImPlotCond cond = ImPlotCond_Once);
  PlotBuilder &SetYAxisLimits(double min, double max,
      ImPlotCond cond = ImPlotCond_Once);
  PlotBuilder &SetXAxisLinks(double *min, double *max);
  PlotBuilder &SetXAxisTicks(std::vector<double> ticks);
  PlotBuilder &SetFlags(ImPlotFlags flags);
  PlotBuilder &SetFixedView(bool enabled = true);
  PlotBuilder &SetFocusedYAxis(bool enabled = true);
  PlotBuilder &SetXAxisLimitsAlways(double min, double max);
  PlotBuilder &SetYAxisLimitsAlways(double min, double max);
  PlotBuilder &SetLegendVisible(bool visible);
  PlotBuilder &SetOffset(int offset);
  PlotBuilder &SetUnderlay(std::function<void()> underlay);
  PlotBuilder &SetOverlay(std::function<void()> overlay);

  // Fluent configuration
  PlotBuilder &Size(Vector2 size);
  PlotBuilder &Width(float width);
  PlotBuilder &Height(float height);
  PlotBuilder &XAxisLabel(std::string label);
  PlotBuilder &YAxisLabel(std::string label);
  PlotBuilder &XAxisFlags(ImPlotAxisFlags flags);
  PlotBuilder &YAxisFlags(ImPlotAxisFlags flags);
  PlotBuilder &XAxisLimits(double min, double max,
      ImPlotCond cond = ImPlotCond_Once);
  PlotBuilder &YAxisLimits(double min, double max,
      ImPlotCond cond = ImPlotCond_Once);
  PlotBuilder &XAxisLinks(double &min, double &max);
  PlotBuilder &XAxisTicks(std::vector<double> ticks);
  PlotBuilder &Flags(ImPlotFlags flags);
  PlotBuilder &FixedView(bool enabled = true);
  PlotBuilder &FocusedYAxis(bool enabled = true);
  PlotBuilder &XAxisLimitsAlways(double min, double max);
  PlotBuilder &YAxisLimitsAlways(double min, double max);
  PlotBuilder &LegendVisible(bool visible);
  PlotBuilder &Offset(int offset);
  PlotBuilder &Underlay(std::function<void()> underlay);
  PlotBuilder &Overlay(std::function<void()> overlay);

  // Line series
  PlotBuilder &AddLine(std::string label, DataView xValues, DataView yValues);
  PlotBuilder &AddLine(std::string label, DataView xValues, DataView yValues,
      int offset);
  PlotBuilder &AddLine(std::string label, DataView yValues);
  PlotBuilder &AddLine(std::string label, DataView yValues, int offset);
  PlotBuilder &AddLine(std::string label, const std::vector<double> &xValues,
      const std::vector<double> &yValues);
  PlotBuilder &AddLine(std::string label, const std::vector<double> &xValues,
      const std::vector<double> &yValues, int offset);
  PlotBuilder &AddLine(std::string label, const std::vector<float> &xValues,
      const std::vector<float> &yValues);
  PlotBuilder &AddLine(std::string label, const std::vector<float> &xValues,
      const std::vector<float> &yValues, int offset);
  PlotBuilder &AddLine(std::string label, const std::vector<double> &yValues);
  PlotBuilder &AddLine(std::string label, const std::vector<double> &yValues,
      int offset);
  PlotBuilder &AddLine(std::string label, const std::vector<float> &yValues);
  PlotBuilder &AddLine(std::string label, const std::vector<float> &yValues,
      int offset);

  // Scatter series
  PlotBuilder &AddScatter(std::string label, DataView xValues,
      DataView yValues);
  PlotBuilder &AddScatter(std::string label, DataView xValues, DataView yValues,
      int offset);
  PlotBuilder &AddScatter(std::string label, const std::vector<double> &xValues,
      const std::vector<double> &yValues);
  PlotBuilder &AddScatter(std::string label, const std::vector<double> &xValues,
      const std::vector<double> &yValues, int offset);
  PlotBuilder &AddScatter(std::string label, const std::vector<float> &xValues,
      const std::vector<float> &yValues);
  PlotBuilder &AddScatter(std::string label, const std::vector<float> &xValues,
      const std::vector<float> &yValues, int offset);

  // Element conversion
  operator UIElement() const;

private:
  // Implementation state
  class Impl;
  std::unique_ptr<Impl> m_Impl;
};

PlotBuilder Plot(std::string title);
} // namespace FlightUI
