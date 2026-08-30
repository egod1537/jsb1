#include "gui/windows/LinearizationWindow.hpp"

#include "sim/linearization/LinearizationResult.hpp"
#include "sim/runtime/SimulationContracts.hpp"
#include "flightui/FlightUI.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr float MatrixCellWidth = 104.0F;
constexpr float MatrixRowLabelWidth = 112.0F;
constexpr float SubsetMatrixHeight = 224.0F;
constexpr float MinimumMatrixHeight = 152.0F;
constexpr float MaximumHeatAlpha = 0.26F;

enum class MatrixColumnKind {
  State,
  Input,
};

enum class DerivativeSource {
  SystemMatrix,
  InputMatrix,
};

struct DerivativeSpec {
  const char *label;
  DerivativeSource source;
  const char *rowState;
  const char *columnName;
};

std::string MakeLabel(const std::vector<std::string> &names, Eigen::Index index,
    const char *fallbackPrefix) {
  const auto nameIndex = static_cast<std::size_t>(index);
  if (nameIndex < names.size() && !names[nameIndex].empty()) {
    return names[nameIndex];
  }

  return std::string(fallbackPrefix) + std::to_string(index);
}

std::vector<Eigen::Index> MakeAllIndices(Eigen::Index count) {
  std::vector<Eigen::Index> indices;
  if (count <= 0) {
    return indices;
  }

  indices.reserve(static_cast<std::size_t>(count));
  for (Eigen::Index index = 0; index < count; ++index) {
    indices.push_back(index);
  }
  return indices;
}

std::vector<Eigen::Index> FilterIndices(std::span<const Eigen::Index> indices,
    Eigen::Index limit) {
  std::vector<Eigen::Index> filtered;
  filtered.reserve(indices.size());
  for (const Eigen::Index index : indices) {
    if (index >= 0 && index < limit) {
      filtered.push_back(index);
    }
  }
  return filtered;
}

double FindHeatScale(const Eigen::MatrixXd &matrix,
    std::span<const Eigen::Index> rowIndices,
    std::span<const Eigen::Index> columnIndices,
    gui::LinearizationValueTransform valueTransform) {
  double maximumMagnitude = 0.0;
  for (const Eigen::Index row : rowIndices) {
    for (const Eigen::Index column : columnIndices) {
      const double transformed =
          gui::TransformLinearizationValue(matrix(row, column), valueTransform);
      if (std::isfinite(transformed)) {
        maximumMagnitude = std::max(maximumMagnitude, std::abs(transformed));
      }
    }
  }
  return maximumMagnitude;
}

float GetHeatIntensity(double transformedValue, double heatScale) {
  if (std::isnan(transformedValue)) {
    return 0.0F;
  }
  if (std::isinf(transformedValue)) {
    return 1.0F;
  }
  if (heatScale <= 0.0) {
    return 0.0F;
  }

  const double normalized =
      std::clamp(std::abs(transformedValue) / heatScale, 0.0, 1.0);
  return static_cast<float>(std::sqrt(normalized));
}

void DrawCellTooltip(const std::string &rowLabel,
    const std::string &columnLabel, MatrixColumnKind columnKind,
    double transformedValue, double rawValue,
    gui::LinearizationValueTransform valueTransform) {
  if (!ImGui::IsItemHovered()) {
    return;
  }

  ImGui::BeginTooltip();
  ImGui::Text("Derivative: %sdot", rowLabel.c_str());
  if (columnKind == MatrixColumnKind::State) {
    ImGui::Text("With respect to: %s", columnLabel.c_str());
  } else {
    ImGui::Text("Input: %s", columnLabel.c_str());
  }
  ImGui::Separator();
  ImGui::Text("Value: % .6e", transformedValue);
  if (valueTransform != gui::LinearizationValueTransform::Raw) {
    ImGui::Text("Raw: % .6e", rawValue);
  }
  ImGui::EndTooltip();
}

void DrawMatrix(const char *tableId, const Eigen::MatrixXd &matrix,
    const std::vector<std::string> &rowNames,
    const std::vector<std::string> &columnNames,
    const char *columnFallbackPrefix, MatrixColumnKind columnKind,
    std::span<const Eigen::Index> requestedRows,
    std::span<const Eigen::Index> requestedColumns,
    gui::LinearizationValueTransform valueTransform,
    float requestedHeight = 0.0F) {
  const std::vector<Eigen::Index> rows =
      FilterIndices(requestedRows, matrix.rows());
  const std::vector<Eigen::Index> columns =
      FilterIndices(requestedColumns, matrix.cols());
  if (rows.empty() || columns.empty()) {
    ImGui::TextDisabled("No matching matrix entries are available.");
    return;
  }

  const int columnCount = static_cast<int>(columns.size()) + 1;
  constexpr ImGuiTableFlags Flags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX
      | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit
      | ImGuiTableFlags_Resizable;
  const float availableHeight =
      std::max(ImGui::GetContentRegionAvail().y, FlightUI::Ui(1.0F));
  const float tableHeight =
      requestedHeight > 0.0F ? requestedHeight : availableHeight;
  if (!ImGui::BeginTable(tableId,
          columnCount,
          Flags,
          ImVec2(0.0F, tableHeight))) {
    return;
  }

  ImGui::TableSetupScrollFreeze(1, 1);
  ImGui::TableSetupColumn("d/dt",
      ImGuiTableColumnFlags_WidthFixed,
      FlightUI::Ui(MatrixRowLabelWidth));
  for (const Eigen::Index column : columns) {
    const std::string label =
        MakeLabel(columnNames, column, columnFallbackPrefix);
    ImGui::TableSetupColumn(label.c_str(),
        ImGuiTableColumnFlags_WidthFixed,
        FlightUI::Ui(MatrixCellWidth));
  }
  ImGui::TableHeadersRow();

  const double heatScale = FindHeatScale(matrix, rows, columns, valueTransform);
  const ImVec4 heatBase =
      FlightUI::GetDarkEditorSemanticColor(FlightUI::SemanticColor::Warning);

  for (const Eigen::Index row : rows) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const std::string rowLabel = MakeLabel(rowNames, row, "x");
    ImGui::TextUnformatted(rowLabel.c_str());

    for (std::size_t visibleColumn = 0; visibleColumn < columns.size();
        ++visibleColumn) {
      const Eigen::Index column = columns[visibleColumn];
      ImGui::TableSetColumnIndex(static_cast<int>(visibleColumn) + 1);

      const double rawValue = matrix(row, column);
      const double transformedValue =
          gui::TransformLinearizationValue(rawValue, valueTransform);
      const float heatIntensity = GetHeatIntensity(transformedValue, heatScale);
      if (heatIntensity > 0.0F) {
        ImVec4 heatColor = heatBase;
        heatColor.w = MaximumHeatAlpha * heatIntensity;
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
            ImGui::GetColorU32(heatColor));
      }

      std::array<char, 32> valueText{};
      std::snprintf(valueText.data(),
          valueText.size(),
          "% .6e",
          transformedValue);
      ImGui::PushID(static_cast<int>(row));
      ImGui::PushID(static_cast<int>(column));
      ImGui::Selectable(valueText.data(), false);
      DrawCellTooltip(rowLabel,
          MakeLabel(columnNames, column, columnFallbackPrefix),
          columnKind,
          transformedValue,
          rawValue,
          valueTransform);
      ImGui::PopID();
      ImGui::PopID();
    }
  }

  ImGui::EndTable();
}

const char *GetTransformLabel(gui::LinearizationValueTransform transform) {
  switch (transform) {
  case gui::LinearizationValueTransform::Raw:
    return "Raw";
  case gui::LinearizationValueTransform::SignedLog10:
    return "Signed log10";
  }
  return "Unknown";
}

void DrawOverview(const gnc::LinearizationResult &result, bool updateInProgress,
    std::string_view errorMessage,
    gui::LinearizationValueTransform valueTransform) {
  constexpr ImGuiTableFlags Flags = ImGuiTableFlags_BordersInnerH
                                    | ImGuiTableFlags_RowBg
                                    | ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("LinearizationOverview", 2, Flags)) {
    return;
  }

  ImGui::TableSetupColumn("Property",
      ImGuiTableColumnFlags_WidthFixed,
      FlightUI::Ui(176.0F));
  ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

  const auto drawTextRow = [](const char *label, const char *value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
  };
  const auto drawSizeRow = [](const char *label, std::size_t value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%llu", static_cast<unsigned long long>(value));
  };
  const auto drawMatrixSizeRow = [](const char *label,
                                     const Eigen::MatrixXd &matrix) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%lld x %lld",
        static_cast<long long>(matrix.rows()),
        static_cast<long long>(matrix.cols()));
  };

  drawMatrixSizeRow("A matrix", result.A);
  drawMatrixSizeRow("B matrix", result.B);
  drawSizeRow("State count", result.stateNames.size());
  drawSizeRow("Input count", result.inputNames.size());

  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextDisabled("Linearization status");
  ImGui::TableSetColumnIndex(1);
  const FlightUI::SemanticColor statusColor =
      updateInProgress        ? FlightUI::SemanticColor::Warning
      : !errorMessage.empty() ? FlightUI::SemanticColor::Error
                              : FlightUI::SemanticColor::Success;
  const char *statusText = updateInProgress        ? "In progress"
                           : !errorMessage.empty() ? "Failed"
                                                   : "Succeeded";
  ImGui::TextColored(FlightUI::GetDarkEditorSemanticColor(statusColor),
      "%s",
      statusText);

  drawTextRow("Value transform", GetTransformLabel(valueTransform));
  ImGui::EndTable();
}

std::optional<double> FindDerivativeValue(
    const gnc::LinearizationResult &result, const DerivativeSpec &spec) {
  const std::optional<std::size_t> row = result.FindStateIndex(spec.rowState);
  if (!row) {
    return std::nullopt;
  }

  const Eigen::Index matrixRow = static_cast<Eigen::Index>(*row);
  if (spec.source == DerivativeSource::SystemMatrix) {
    const std::optional<std::size_t> column =
        result.FindStateIndex(spec.columnName);
    if (!column || matrixRow >= result.A.rows()
        || static_cast<Eigen::Index>(*column) >= result.A.cols()) {
      return std::nullopt;
    }
    return result.A(matrixRow, static_cast<Eigen::Index>(*column));
  }

  const std::optional<std::size_t> column =
      result.FindInputIndex(spec.columnName);
  if (!column || matrixRow >= result.B.rows()
      || static_cast<Eigen::Index>(*column) >= result.B.cols()) {
    return std::nullopt;
  }
  return result.B(matrixRow, static_cast<Eigen::Index>(*column));
}

void DrawDerivativeGroup(const char *title, const char *tableId,
    std::span<const DerivativeSpec> derivatives,
    const gnc::LinearizationResult &result,
    gui::LinearizationValueTransform valueTransform) {
  ImGui::SeparatorText(title);
  constexpr ImGuiTableFlags Flags = ImGuiTableFlags_BordersInnerH
                                    | ImGuiTableFlags_RowBg
                                    | ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable(tableId, 2, Flags)) {
    return;
  }

  ImGui::TableSetupColumn("Derivative", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Value",
      ImGuiTableColumnFlags_WidthFixed,
      FlightUI::Ui(MatrixCellWidth));
  for (const DerivativeSpec &derivative : derivatives) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(derivative.label);
    ImGui::TableSetColumnIndex(1);

    const std::optional<double> rawValue =
        FindDerivativeValue(result, derivative);
    if (!rawValue) {
      ImGui::TextDisabled("N/A");
      continue;
    }

    const double displayValue =
        gui::TransformLinearizationValue(*rawValue, valueTransform);
    ImGui::Text("% .6e", displayValue);
    if (valueTransform != gui::LinearizationValueTransform::Raw
        && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Raw: % .6e", *rawValue);
    }
  }
  ImGui::EndTable();
}

void DrawDynamics(const gnc::LinearizationResult &result,
    gui::LinearizationValueTransform valueTransform) {
  constexpr std::array RollDerivatives{
      DerivativeSpec{"∂Pdot / ∂P", DerivativeSource::SystemMatrix, "P", "P"},
      DerivativeSpec{"∂Pdot / ∂DaCmd",
          DerivativeSource::InputMatrix,
          "P",
          "DaCmd"},
  };
  constexpr std::array PitchDerivatives{
      DerivativeSpec{"∂Qdot / ∂Alpha",
          DerivativeSource::SystemMatrix,
          "Q",
          "Alpha"},
      DerivativeSpec{"∂Qdot / ∂Q", DerivativeSource::SystemMatrix, "Q", "Q"},
      DerivativeSpec{"∂Qdot / ∂DeCmd",
          DerivativeSource::InputMatrix,
          "Q",
          "DeCmd"},
  };
  constexpr std::array YawDerivatives{
      DerivativeSpec{"∂Betadot / ∂Beta",
          DerivativeSource::SystemMatrix,
          "Beta",
          "Beta"},
      DerivativeSpec{"∂Betadot / ∂R",
          DerivativeSource::SystemMatrix,
          "Beta",
          "R"},
      DerivativeSpec{"∂Rdot / ∂Beta",
          DerivativeSource::SystemMatrix,
          "R",
          "Beta"},
      DerivativeSpec{"∂Rdot / ∂R", DerivativeSource::SystemMatrix, "R", "R"},
      DerivativeSpec{"∂Rdot / ∂DrCmd",
          DerivativeSource::InputMatrix,
          "R",
          "DrCmd"},
  };

  DrawDerivativeGroup("Roll",
      "RollDerivatives",
      RollDerivatives,
      result,
      valueTransform);
  DrawDerivativeGroup("Pitch",
      "PitchDerivatives",
      PitchDerivatives,
      result,
      valueTransform);
  DrawDerivativeGroup("Yaw",
      "YawDerivatives",
      YawDerivatives,
      result,
      valueTransform);
}

void AppendUnique(std::vector<Eigen::Index> &indices, std::size_t index) {
  const Eigen::Index matrixIndex = static_cast<Eigen::Index>(index);
  if (std::find(indices.begin(), indices.end(), matrixIndex) == indices.end()) {
    indices.push_back(matrixIndex);
  }
}

void AppendFirstState(const gnc::LinearizationResult &result,
    std::vector<Eigen::Index> &indices,
    std::initializer_list<std::string_view> aliases) {
  for (const std::string_view alias : aliases) {
    if (const auto index = result.FindStateIndex(alias)) {
      AppendUnique(indices, *index);
      return;
    }
  }
}

bool AppendFirstInput(const gnc::LinearizationResult &result,
    std::vector<Eigen::Index> &indices,
    std::initializer_list<std::string_view> aliases) {
  for (const std::string_view alias : aliases) {
    if (const auto index = result.FindInputIndex(alias)) {
      AppendUnique(indices, *index);
      return true;
    }
  }
  return false;
}

std::string Lowercase(std::string_view text) {
  std::string lowercase;
  lowercase.reserve(text.size());
  for (const char character : text) {
    lowercase.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return lowercase;
}

void AppendThrottleInput(const gnc::LinearizationResult &result,
    std::vector<Eigen::Index> &indices) {
  if (AppendFirstInput(result,
          indices,
          {"DtCmd", "ThtlCmd", "ThrottleCmd", "Throttle"})) {
    return;
  }

  for (std::size_t index = 0; index < result.inputNames.size(); ++index) {
    const std::string lowercase = Lowercase(result.inputNames[index]);
    if (lowercase.find("throttle") != std::string::npos
        || lowercase.find("thtl") != std::string::npos) {
      AppendUnique(indices, index);
      return;
    }
  }
}

std::vector<Eigen::Index> MakeLongitudinalStates(
    const gnc::LinearizationResult &result) {
  std::vector<Eigen::Index> indices;
  AppendFirstState(result, indices, {"Va", "Vt"});
  AppendFirstState(result, indices, {"Alpha"});
  AppendFirstState(result, indices, {"Q"});
  AppendFirstState(result, indices, {"Theta"});
  return indices;
}

std::vector<Eigen::Index> MakeLongitudinalInputs(
    const gnc::LinearizationResult &result) {
  std::vector<Eigen::Index> indices;
  AppendFirstInput(result, indices, {"DeCmd"});
  AppendThrottleInput(result, indices);
  return indices;
}

std::vector<Eigen::Index> MakeLateralStates(
    const gnc::LinearizationResult &result) {
  std::vector<Eigen::Index> indices;
  AppendFirstState(result, indices, {"Beta"});
  AppendFirstState(result, indices, {"P"});
  AppendFirstState(result, indices, {"R"});
  AppendFirstState(result, indices, {"Phi"});
  return indices;
}

std::vector<Eigen::Index> MakeLateralInputs(
    const gnc::LinearizationResult &result) {
  std::vector<Eigen::Index> indices;
  AppendFirstInput(result, indices, {"DaCmd"});
  AppendFirstInput(result, indices, {"DrCmd"});
  return indices;
}

void DrawSelectionSummary(const char *label,
    const std::vector<std::string> &names,
    std::span<const Eigen::Index> indices) {
  std::string summary;
  for (const Eigen::Index index : indices) {
    if (!summary.empty()) {
      summary += " / ";
    }
    summary += MakeLabel(names, index, "?");
  }

  ImGui::TextDisabled("%s: %s",
      label,
      summary.empty() ? "None" : summary.c_str());
}

void DrawSubsetMatrices(const char *idPrefix,
    const gnc::LinearizationResult &result,
    std::span<const Eigen::Index> states, std::span<const Eigen::Index> inputs,
    gui::LinearizationValueTransform valueTransform) {
  DrawSelectionSummary("States", result.stateNames, states);
  DrawSelectionSummary("Inputs", result.inputNames, inputs);

  ImGui::SeparatorText("A - State dynamics");
  const std::string systemTableId = std::string(idPrefix) + "SystemMatrix";
  DrawMatrix(systemTableId.c_str(),
      result.A,
      result.stateNames,
      result.stateNames,
      "x",
      MatrixColumnKind::State,
      states,
      states,
      valueTransform,
      FlightUI::Ui(SubsetMatrixHeight));

  ImGui::SeparatorText("B - Control inputs");
  const std::string inputTableId = std::string(idPrefix) + "InputMatrix";
  DrawMatrix(inputTableId.c_str(),
      result.B,
      result.stateNames,
      result.inputNames,
      "u",
      MatrixColumnKind::Input,
      states,
      inputs,
      valueTransform,
      std::max(ImGui::GetContentRegionAvail().y,
          FlightUI::Ui(MinimumMatrixHeight)));
}
} // namespace

namespace gui {
LinearizationWindow::LinearizationWindow(LinearizationController &controller)
    : Window("FG Linearization", EditorIconAliases::Linearization),
      controller_(controller) {}

void LinearizationWindow::OnRender(const sim::SimulationSnapshot &snapshot) {
  const sim::LinearizationSnapshot &linearization = snapshot.linearization;
  if (!linearization.available) {
    ImGui::TextDisabled("Linearization is not available for this autopilot.");
    return;
  }

  bool automaticUpdates = linearization.automaticUpdatesEnabled;
  if (ImGui::Checkbox("Automatic linearization", &automaticUpdates)) {
    controller_.Handle(AutomaticLinearizationChanged{automaticUpdates});
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Run asynchronous aircraft linearization every 5 seconds");
  }
  ImGui::SameLine();

  const bool inProgress = linearization.updateInProgress;
  const std::string_view errorMessage = linearization.errorMessage;
  if (!automaticUpdates) {
    const FlightUI::UIElement badge =
        FlightUI::StatusBadge("Off", FlightUI::StatusTone::Neutral);
    badge.Render();
    ImGui::SameLine();
    ImGui::TextDisabled(inProgress ? "Off (current worker is finishing)"
                                   : "Off (latest result retained)");
  } else if (inProgress) {
    const FlightUI::UIElement badge =
        FlightUI::StatusBadge("Updating", FlightUI::StatusTone::Warning);
    badge.Render();
  } else if (!errorMessage.empty()) {
    const FlightUI::UIElement badge =
        FlightUI::StatusBadge("Failed", FlightUI::StatusTone::Error);
    badge.Render();
    ImGui::SameLine();
    ImGui::TextColored(
        FlightUI::GetDarkEditorSemanticColor(FlightUI::SemanticColor::Error),
        "%.*s",
        static_cast<int>(errorMessage.size()),
        errorMessage.data());
  } else {
    const bool hasResult = linearization.result.has_value();
    const FlightUI::UIElement badge = FlightUI::StatusBadge(
        hasResult ? "Ready" : "Waiting",
        hasResult ? FlightUI::StatusTone::Success : FlightUI::StatusTone::Info);
    badge.Render();
  }

  DrawTransformSelector();
  ImGui::Separator();
  if (!linearization.result.has_value()) {
    ImGui::TextDisabled("No periodic result is available yet.");
    ImGui::TextDisabled("Waiting for the asynchronous periodic update.");
    return;
  }

  DrawResult(*linearization.result, inProgress, errorMessage);
}

void LinearizationWindow::DrawTransformSelector() {
  const LinearizationValueTransform valueTransform =
      controller_.GetModel().valueTransform;
  int selectedTransform = static_cast<int>(valueTransform);
  ImGui::SetNextItemWidth(FlightUI::Ui(176.0F));
  if (ImGui::Combo("Value transform",
          &selectedTransform,
          "Raw\0Signed log10\0")) {
    controller_.Handle(LinearizationValueTransformChanged{
        static_cast<LinearizationValueTransform>(selectedTransform)});
  }

  if (controller_.GetModel().valueTransform
      == LinearizationValueTransform::SignedLog10) {
    ImGui::SameLine();
    ImGui::TextDisabled("sign(x) log10(1 + |x|)");
  }
}

void LinearizationWindow::DrawResult(const gnc::LinearizationResult &result,
    bool updateInProgress, std::string_view errorMessage) const {
  const LinearizationValueTransform valueTransform =
      controller_.GetModel().valueTransform;
  if (!ImGui::BeginTabBar("LinearizationViews")) {
    return;
  }

  if (ImGui::BeginTabItem("Overview")) {
    DrawOverview(result, updateInProgress, errorMessage, valueTransform);
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Dynamics")) {
    DrawDynamics(result, valueTransform);
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Longitudinal")) {
    const std::vector<Eigen::Index> states = MakeLongitudinalStates(result);
    const std::vector<Eigen::Index> inputs = MakeLongitudinalInputs(result);
    DrawSubsetMatrices("Longitudinal", result, states, inputs, valueTransform);
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Lateral")) {
    const std::vector<Eigen::Index> states = MakeLateralStates(result);
    const std::vector<Eigen::Index> inputs = MakeLateralInputs(result);
    DrawSubsetMatrices("Lateral", result, states, inputs, valueTransform);
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Full A")) {
    const std::vector<Eigen::Index> rows = MakeAllIndices(result.A.rows());
    const std::vector<Eigen::Index> columns = MakeAllIndices(result.A.cols());
    DrawMatrix("FullSystemMatrix",
        result.A,
        result.stateNames,
        result.stateNames,
        "x",
        MatrixColumnKind::State,
        rows,
        columns,
        valueTransform);
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Full B")) {
    const std::vector<Eigen::Index> rows = MakeAllIndices(result.B.rows());
    const std::vector<Eigen::Index> columns = MakeAllIndices(result.B.cols());
    DrawMatrix("FullInputMatrix",
        result.B,
        result.stateNames,
        result.inputNames,
        "u",
        MatrixColumnKind::Input,
        rows,
        columns,
        valueTransform);
    ImGui::EndTabItem();
  }

  ImGui::EndTabBar();
}
} // namespace gui
