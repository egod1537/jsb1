#include "flightui/plot/Plot.hpp"

#include "flightui/core/UIElementFactory.hpp"
#include "flightui/core/UIScale.hpp"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <implot.h>
#include <implot_internal.h>
#include <limits>
#include <optional>
#include <utility>

namespace FlightUI {
namespace {
constexpr double YAxisPaddingRatio = 0.10;
constexpr double MinimumYAxisPadding = 0.10;
constexpr double NearlyConstantYAxisRatio = 1.0e-6;

enum class SeriesType {
  Line,
  Scatter,
};

struct PlotSeries {
  SeriesType Type = SeriesType::Line;
  std::string Label;
  DataView XValues;
  DataView YValues;
  bool HasXValues = false;
  int Offset = 0;
};

struct AxisLimits {
  double Min = 0.0;
  double Max = 1.0;
  ImPlotCond Condition = ImPlotCond_Once;
  bool Enabled = false;
};

struct AxisLinks {
  double *Min = nullptr;
  double *Max = nullptr;
};

struct DataRange {
  double Min = std::numeric_limits<double>::infinity();
  double Max = -std::numeric_limits<double>::infinity();
  bool HasValue = false;
};

std::size_t GetSeriesCount(const PlotSeries &series);

void IncludeFiniteValue(DataRange &range, double value) {
  if (!std::isfinite(value)) {
    return;
  }

  range.Min = std::min(range.Min, value);
  range.Max = std::max(range.Max, value);
  range.HasValue = true;
}

std::optional<double> ReadValue(const DataView &values, std::size_t index) {
  if (values.GetData() == nullptr || index >= values.GetCount()) {
    return std::nullopt;
  }

  const auto *bytes = static_cast<const std::byte *>(values.GetData());
  const std::byte *valueAddress = bytes + index * values.GetStride();
  switch (values.GetType()) {
  case DataType::Double: {
    double value = 0.0;
    std::memcpy(&value, valueAddress, sizeof(value));
    return value;
  }
  case DataType::Float: {
    float value = 0.0F;
    std::memcpy(&value, valueAddress, sizeof(value));
    return static_cast<double>(value);
  }
  case DataType::None:
    return std::nullopt;
  }

  return std::nullopt;
}

std::size_t NormalizeOffset(int offset, std::size_t count) {
  if (count == 0) {
    return 0;
  }

  const auto signedCount = static_cast<std::int64_t>(count);
  const std::int64_t normalized =
      (static_cast<std::int64_t>(offset) % signedCount + signedCount)
      % signedCount;
  return static_cast<std::size_t>(normalized);
}

std::optional<PlotAxisRange> CalculateFocusedYAxisRange(
    const std::vector<PlotSeries> &seriesList, ImPlotAxisFlags yAxisFlags) {
  const bool rangeFit = (yAxisFlags & ImPlotAxisFlags_RangeFit) != 0;
  const ImPlotRange visibleXRange = ImPlot::GetPlotLimits().X;
  DataRange range;

  for (const PlotSeries &series : seriesList) {
    const ImPlotItem *item = ImPlot::GetItem(series.Label.c_str());
    if (item != nullptr && !item->Show) {
      continue;
    }

    const std::size_t count = GetSeriesCount(series);
    if (count == 0 || series.YValues.GetData() == nullptr) {
      continue;
    }

    const std::size_t offset = NormalizeOffset(series.Offset, count);
    for (std::size_t logicalIndex = 0; logicalIndex < count; ++logicalIndex) {
      const std::size_t dataIndex = (logicalIndex + offset) % count;
      const std::optional<double> yValue = ReadValue(series.YValues, dataIndex);
      if (!yValue.has_value() || !std::isfinite(*yValue)) {
        continue;
      }

      if (rangeFit) {
        const std::optional<double> xValue =
            series.HasXValues
                ? ReadValue(series.XValues, dataIndex)
                : std::optional<double>(static_cast<double>(logicalIndex));
        if (!xValue.has_value() || !std::isfinite(*xValue)
            || !visibleXRange.Contains(*xValue)) {
          continue;
        }
      }

      IncludeFiniteValue(range, *yValue);
    }
  }

  if (!range.HasValue) {
    return std::nullopt;
  }
  return ExpandYAxisRange(range.Min, range.Max);
}

void AddFocusedYAxisFitBounds(const std::vector<PlotSeries> &seriesList,
    ImPlotAxisFlags yAxisFlags) {
  if ((yAxisFlags & ImPlotAxisFlags_AutoFit) == 0) {
    return;
  }

  const std::optional<PlotAxisRange> range =
      CalculateFocusedYAxisRange(seriesList, yAxisFlags);
  if (!range.has_value()) {
    return;
  }

  const ImPlotRange visibleXRange = ImPlot::GetPlotLimits().X;
  const double x =
      visibleXRange.Min + (visibleXRange.Max - visibleXRange.Min) * 0.5;
  if (!std::isfinite(x)) {
    return;
  }

  const double xValues[] = {x, x};
  const double yValues[] = {range->Min, range->Max};
  ImPlotSpec spec;
  spec.LineColor = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
  spec.LineWeight = 0.0F;
  spec.Flags = ImPlotItemFlags_NoLegend;
  ImPlot::PlotLine("##FlightUIFocusedYAxisBounds", xValues, yValues, 2, spec);
}

ImVec2 ToImVec2(Vector2 value) { return ImVec2(value.X, value.Y); }

int ToPlotCount(std::size_t count) {
  return static_cast<int>(
      std::min<std::size_t>(count, static_cast<std::size_t>(INT_MAX)));
}

std::size_t GetSeriesCount(const PlotSeries &series) {
  if (!series.HasXValues) {
    return series.YValues.GetCount();
  }

  if (series.XValues.GetCount() != series.YValues.GetCount()) {
    assert(false && "Plot x and y value counts must match");
  }

  return std::min(series.XValues.GetCount(), series.YValues.GetCount());
}

ImPlotSpec MakeSeriesSpec(const PlotSeries &series) {
  ImPlotSpec spec;
  spec.Offset = series.Offset;
  spec.Stride = static_cast<int>(series.YValues.GetStride());
  return spec;
}

void PlotLineWithoutX(const PlotSeries &series, int count) {
  const ImPlotSpec spec = MakeSeriesSpec(series);
  switch (series.YValues.GetType()) {
  case DataType::Double:
    ImPlot::PlotLine(series.Label.c_str(),
        static_cast<const double *>(series.YValues.GetData()),
        count,
        1.0,
        0.0,
        spec);
    break;
  case DataType::Float:
    ImPlot::PlotLine(series.Label.c_str(),
        static_cast<const float *>(series.YValues.GetData()),
        count,
        1.0,
        0.0,
        spec);
    break;
  case DataType::None:
    break;
  }
}

void PlotLineWithX(const PlotSeries &series, int count) {
  if (series.XValues.GetType() != series.YValues.GetType()) {
    assert(false && "Plot x and y value types must match");
    return;
  }
  if (series.XValues.GetStride() != series.YValues.GetStride()) {
    assert(false && "Plot x and y value strides must match");
    return;
  }

  const ImPlotSpec spec = MakeSeriesSpec(series);
  switch (series.YValues.GetType()) {
  case DataType::Double:
    ImPlot::PlotLine(series.Label.c_str(),
        static_cast<const double *>(series.XValues.GetData()),
        static_cast<const double *>(series.YValues.GetData()),
        count,
        spec);
    break;
  case DataType::Float:
    ImPlot::PlotLine(series.Label.c_str(),
        static_cast<const float *>(series.XValues.GetData()),
        static_cast<const float *>(series.YValues.GetData()),
        count,
        spec);
    break;
  case DataType::None:
    break;
  }
}

void PlotScatterWithX(const PlotSeries &series, int count) {
  if (series.XValues.GetType() != series.YValues.GetType()) {
    assert(false && "Plot x and y value types must match");
    return;
  }
  if (series.XValues.GetStride() != series.YValues.GetStride()) {
    assert(false && "Plot x and y value strides must match");
    return;
  }

  const ImPlotSpec spec = MakeSeriesSpec(series);
  switch (series.YValues.GetType()) {
  case DataType::Double:
    ImPlot::PlotScatter(series.Label.c_str(),
        static_cast<const double *>(series.XValues.GetData()),
        static_cast<const double *>(series.YValues.GetData()),
        count,
        spec);
    break;
  case DataType::Float:
    ImPlot::PlotScatter(series.Label.c_str(),
        static_cast<const float *>(series.XValues.GetData()),
        static_cast<const float *>(series.YValues.GetData()),
        count,
        spec);
    break;
  case DataType::None:
    break;
  }
}

void PlotSeriesData(const PlotSeries &series) {
  const std::size_t count = GetSeriesCount(series);
  if (count == 0 || series.YValues.GetData() == nullptr) {
    return;
  }

  const int plotCount = ToPlotCount(count);
  if (series.Type == SeriesType::Scatter) {
    if (series.HasXValues && series.XValues.GetData() != nullptr) {
      PlotScatterWithX(series, plotCount);
    }
    return;
  }

  if (series.HasXValues && series.XValues.GetData() != nullptr) {
    PlotLineWithX(series, plotCount);
    return;
  }

  PlotLineWithoutX(series, plotCount);
}
} // namespace

std::optional<PlotAxisRange> ExpandYAxisRange(double minValue,
    double maxValue) {
  if (!std::isfinite(minValue) || !std::isfinite(maxValue)
      || maxValue < minValue) {
    return std::nullopt;
  }

  const double range = maxValue - minValue;
  const double scale = std::max({1.0, std::abs(minValue), std::abs(maxValue)});
  const bool nearlyConstant = range <= scale * NearlyConstantYAxisRatio;
  const double padding =
      nearlyConstant
          ? std::max(std::max(std::abs(minValue), std::abs(maxValue)) * 0.05,
                MinimumYAxisPadding)
          : range * YAxisPaddingRatio;

  const double expandedMin = minValue - padding;
  const double expandedMax = maxValue + padding;
  if (!std::isfinite(expandedMin) || !std::isfinite(expandedMax)) {
    return std::nullopt;
  }

  return PlotAxisRange{expandedMin, expandedMax};
}

class PlotBuilder::Impl {
public:
  std::string Title;
  Vector2 Size{-1.0F, 0.0F};
  std::string XAxisLabel;
  std::string YAxisLabel;
  ImPlotAxisFlags XAxisFlags = ImPlotAxisFlags_None;
  ImPlotAxisFlags YAxisFlags = ImPlotAxisFlags_None;
  AxisLimits XAxisLimits;
  AxisLimits YAxisLimits;
  AxisLinks XAxisLinks;
  std::vector<double> XAxisTicks;
  ImPlotFlags Flags = ImPlotFlags_None;
  bool LegendVisible = true;
  int Offset = 0;
  std::vector<PlotSeries> SeriesList;
  std::function<void()> Underlay;
  std::function<void()> Overlay;
};

PlotBuilder::PlotBuilder(std::string title) : m_Impl(std::make_unique<Impl>()) {
  m_Impl->Title = std::move(title);
}

PlotBuilder::PlotBuilder(const PlotBuilder &other)
    : m_Impl(other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl)) {}

PlotBuilder::PlotBuilder(PlotBuilder &&other) noexcept = default;

PlotBuilder &PlotBuilder::operator=(const PlotBuilder &other) {
  if (this != &other) {
    m_Impl = other.m_Impl == nullptr ? nullptr
                                     : std::make_unique<Impl>(*other.m_Impl);
  }

  return *this;
}

PlotBuilder &PlotBuilder::operator=(PlotBuilder &&other) noexcept = default;

PlotBuilder::~PlotBuilder() = default;

PlotBuilder &PlotBuilder::SetSize(Vector2 size) {
  m_Impl->Size = size;
  return *this;
}

PlotBuilder &PlotBuilder::SetWidth(float width) {
  m_Impl->Size.X = width;
  return *this;
}

PlotBuilder &PlotBuilder::SetHeight(float height) {
  m_Impl->Size.Y = height;
  return *this;
}

PlotBuilder &PlotBuilder::SetXAxisLabel(std::string label) {
  m_Impl->XAxisLabel = std::move(label);
  return *this;
}

PlotBuilder &PlotBuilder::SetYAxisLabel(std::string label) {
  m_Impl->YAxisLabel = std::move(label);
  return *this;
}

PlotBuilder &PlotBuilder::SetXAxisFlags(ImPlotAxisFlags flags) {
  m_Impl->XAxisFlags = flags;
  return *this;
}

PlotBuilder &PlotBuilder::SetYAxisFlags(ImPlotAxisFlags flags) {
  m_Impl->YAxisFlags = flags;
  return *this;
}

PlotBuilder &PlotBuilder::SetXAxisLimits(double min, double max,
    ImPlotCond cond) {
  m_Impl->XAxisLimits = AxisLimits{min, max, cond, true};
  return *this;
}

PlotBuilder &PlotBuilder::SetYAxisLimits(double min, double max,
    ImPlotCond cond) {
  m_Impl->YAxisLimits = AxisLimits{min, max, cond, true};
  return *this;
}

PlotBuilder &PlotBuilder::SetXAxisLinks(double *min, double *max) {
  m_Impl->XAxisLinks = AxisLinks{min, max};
  return *this;
}

PlotBuilder &PlotBuilder::SetXAxisTicks(std::vector<double> ticks) {
  m_Impl->XAxisTicks = std::move(ticks);
  return *this;
}

PlotBuilder &PlotBuilder::SetFlags(ImPlotFlags flags) {
  m_Impl->Flags = flags;
  return *this;
}

PlotBuilder &PlotBuilder::SetFixedView(bool enabled) {
  constexpr ImPlotFlags FixedViewFlags =
      ImPlotFlags_NoInputs | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect
      | ImPlotFlags_NoTitle;

  if (enabled) {
    m_Impl->Flags |= FixedViewFlags;
  } else {
    m_Impl->Flags &= ~FixedViewFlags;
  }

  return *this;
}

PlotBuilder &PlotBuilder::SetFocusedYAxis(bool enabled) {
  constexpr ImPlotAxisFlags FocusedYAxisFlags =
      ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;

  if (enabled) {
    m_Impl->YAxisFlags |= FocusedYAxisFlags;
  } else {
    m_Impl->YAxisFlags &= ~FocusedYAxisFlags;
  }

  return *this;
}

PlotBuilder &PlotBuilder::SetXAxisLimitsAlways(double min, double max) {
  return SetXAxisLimits(min, max, ImPlotCond_Always);
}

PlotBuilder &PlotBuilder::SetYAxisLimitsAlways(double min, double max) {
  return SetYAxisLimits(min, max, ImPlotCond_Always);
}

PlotBuilder &PlotBuilder::SetLegendVisible(bool visible) {
  m_Impl->LegendVisible = visible;
  return *this;
}

PlotBuilder &PlotBuilder::SetOffset(int offset) {
  m_Impl->Offset = offset;
  for (PlotSeries &series : m_Impl->SeriesList) {
    series.Offset = offset;
  }
  return *this;
}

PlotBuilder &PlotBuilder::SetUnderlay(std::function<void()> underlay) {
  m_Impl->Underlay = std::move(underlay);
  return *this;
}

PlotBuilder &PlotBuilder::SetOverlay(std::function<void()> overlay) {
  m_Impl->Overlay = std::move(overlay);
  return *this;
}

PlotBuilder &PlotBuilder::Size(Vector2 size) { return SetSize(size); }

PlotBuilder &PlotBuilder::Width(float width) { return SetWidth(width); }

PlotBuilder &PlotBuilder::Height(float height) { return SetHeight(height); }

PlotBuilder &PlotBuilder::XAxisLabel(std::string label) {
  return SetXAxisLabel(std::move(label));
}

PlotBuilder &PlotBuilder::YAxisLabel(std::string label) {
  return SetYAxisLabel(std::move(label));
}

PlotBuilder &PlotBuilder::XAxisFlags(ImPlotAxisFlags flags) {
  return SetXAxisFlags(flags);
}

PlotBuilder &PlotBuilder::YAxisFlags(ImPlotAxisFlags flags) {
  return SetYAxisFlags(flags);
}

PlotBuilder &PlotBuilder::XAxisLimits(double min, double max, ImPlotCond cond) {
  return SetXAxisLimits(min, max, cond);
}

PlotBuilder &PlotBuilder::YAxisLimits(double min, double max, ImPlotCond cond) {
  return SetYAxisLimits(min, max, cond);
}

PlotBuilder &PlotBuilder::XAxisLinks(double &min, double &max) {
  return SetXAxisLinks(&min, &max);
}

PlotBuilder &PlotBuilder::XAxisTicks(std::vector<double> ticks) {
  return SetXAxisTicks(std::move(ticks));
}

PlotBuilder &PlotBuilder::Flags(ImPlotFlags flags) { return SetFlags(flags); }

PlotBuilder &PlotBuilder::FixedView(bool enabled) {
  return SetFixedView(enabled);
}

PlotBuilder &PlotBuilder::FocusedYAxis(bool enabled) {
  return SetFocusedYAxis(enabled);
}

PlotBuilder &PlotBuilder::XAxisLimitsAlways(double min, double max) {
  return SetXAxisLimitsAlways(min, max);
}

PlotBuilder &PlotBuilder::YAxisLimitsAlways(double min, double max) {
  return SetYAxisLimitsAlways(min, max);
}

PlotBuilder &PlotBuilder::LegendVisible(bool visible) {
  return SetLegendVisible(visible);
}

PlotBuilder &PlotBuilder::Offset(int offset) { return SetOffset(offset); }

PlotBuilder &PlotBuilder::Underlay(std::function<void()> underlay) {
  return SetUnderlay(std::move(underlay));
}

PlotBuilder &PlotBuilder::Overlay(std::function<void()> overlay) {
  return SetOverlay(std::move(overlay));
}

PlotBuilder &PlotBuilder::AddLine(std::string label, DataView xValues,
    DataView yValues) {
  return AddLine(std::move(label), xValues, yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label, DataView xValues,
    DataView yValues, int offset) {
  m_Impl->SeriesList.push_back(PlotSeries{SeriesType::Line,
      std::move(label),
      xValues,
      yValues,
      true,
      offset});
  return *this;
}

PlotBuilder &PlotBuilder::AddLine(std::string label, DataView yValues) {
  return AddLine(std::move(label), yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label, DataView yValues,
    int offset) {
  m_Impl->SeriesList.push_back(PlotSeries{SeriesType::Line,
      std::move(label),
      DataView(),
      yValues,
      false,
      offset});
  return *this;
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<double> &xValues, const std::vector<double> &yValues) {
  return AddLine(std::move(label), xValues, yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<double> &xValues, const std::vector<double> &yValues,
    int offset) {
  return AddLine(std::move(label),
      DataView::From(xValues),
      DataView::From(yValues),
      offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<float> &xValues, const std::vector<float> &yValues) {
  return AddLine(std::move(label), xValues, yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<float> &xValues, const std::vector<float> &yValues,
    int offset) {
  return AddLine(std::move(label),
      DataView::From(xValues),
      DataView::From(yValues),
      offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<double> &yValues) {
  return AddLine(std::move(label), yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<double> &yValues, int offset) {
  return AddLine(std::move(label), DataView::From(yValues), offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<float> &yValues) {
  return AddLine(std::move(label), yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddLine(std::string label,
    const std::vector<float> &yValues, int offset) {
  return AddLine(std::move(label), DataView::From(yValues), offset);
}

PlotBuilder &PlotBuilder::AddScatter(std::string label, DataView xValues,
    DataView yValues) {
  return AddScatter(std::move(label), xValues, yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddScatter(std::string label, DataView xValues,
    DataView yValues, int offset) {
  m_Impl->SeriesList.push_back(PlotSeries{SeriesType::Scatter,
      std::move(label),
      xValues,
      yValues,
      true,
      offset});
  return *this;
}

PlotBuilder &PlotBuilder::AddScatter(std::string label,
    const std::vector<double> &xValues, const std::vector<double> &yValues) {
  return AddScatter(std::move(label), xValues, yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddScatter(std::string label,
    const std::vector<double> &xValues, const std::vector<double> &yValues,
    int offset) {
  return AddScatter(std::move(label),
      DataView::From(xValues),
      DataView::From(yValues),
      offset);
}

PlotBuilder &PlotBuilder::AddScatter(std::string label,
    const std::vector<float> &xValues, const std::vector<float> &yValues) {
  return AddScatter(std::move(label), xValues, yValues, m_Impl->Offset);
}

PlotBuilder &PlotBuilder::AddScatter(std::string label,
    const std::vector<float> &xValues, const std::vector<float> &yValues,
    int offset) {
  return AddScatter(std::move(label),
      DataView::From(xValues),
      DataView::From(yValues),
      offset);
}

PlotBuilder::operator UIElement() const {
  Impl state = *m_Impl;
  return CreateElement([state] {
    ImPlotFlags flags = state.Flags;
    if (!state.LegendVisible) {
      flags |= ImPlotFlags_NoLegend;
    }

    if (ImPlot::BeginPlot(state.Title.c_str(),
            ToImVec2(UiSize(state.Size)),
            flags)) {
      const char *xLabel =
          state.XAxisLabel.empty() ? nullptr : state.XAxisLabel.c_str();
      const char *yLabel =
          state.YAxisLabel.empty() ? nullptr : state.YAxisLabel.c_str();
      ImPlot::SetupAxes(xLabel, yLabel, state.XAxisFlags, state.YAxisFlags);
      if (state.XAxisLinks.Min != nullptr || state.XAxisLinks.Max != nullptr) {
        ImPlot::SetupAxisLinks(ImAxis_X1,
            state.XAxisLinks.Min,
            state.XAxisLinks.Max);
      }
      if (state.XAxisLimits.Enabled) {
        ImPlot::SetupAxisLimits(ImAxis_X1,
            state.XAxisLimits.Min,
            state.XAxisLimits.Max,
            state.XAxisLimits.Condition);
      }
      if (state.YAxisLimits.Enabled) {
        ImPlot::SetupAxisLimits(ImAxis_Y1,
            state.YAxisLimits.Min,
            state.YAxisLimits.Max,
            state.YAxisLimits.Condition);
      }
      if (!state.XAxisTicks.empty()) {
        ImPlot::SetupAxisTicks(ImAxis_X1,
            state.XAxisTicks.data(),
            ToPlotCount(state.XAxisTicks.size()));
      }

      if (state.Underlay) {
        state.Underlay();
      }

      for (const PlotSeries &series : state.SeriesList) {
        PlotSeriesData(series);
      }
      AddFocusedYAxisFitBounds(state.SeriesList, state.YAxisFlags);

      if (state.Overlay) {
        state.Overlay();
      }

      ImPlot::EndPlot();
    }
  });
}

PlotBuilder Plot(std::string title) { return PlotBuilder(std::move(title)); }
} // namespace FlightUI
