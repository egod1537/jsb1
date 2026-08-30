#include "flightui/controls/ValueLabel.hpp"

#include "flightui/core/UIElementFactory.hpp"

#include <array>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace FlightUI {
namespace {
enum class ValueType {
  FloatingPoint,
  Integer,
};

char DefaultConversion(ValueType valueType) {
  return valueType == ValueType::Integer ? 'd' : 'g';
}

bool HasPrintfConversion(std::string_view spec) {
  if (spec.empty()) {
    return false;
  }

  constexpr std::string_view Conversions = "diuoxXfFeEgGaAcsp";
  return Conversions.find(spec.back()) != std::string_view::npos;
}

std::string NormalizeFormat(std::string format, ValueType valueType) {
  const std::size_t open = format.find('{');
  if (open == std::string::npos) {
    return format;
  }

  const std::size_t close = format.find('}', open + 1);
  if (close == std::string::npos) {
    return format;
  }

  std::string_view field(format.data() + open + 1, close - open - 1);
  if (!field.empty() && field.front() != ':') {
    return format;
  }

  std::string_view spec;
  if (!field.empty()) {
    spec = field.substr(1);
  }

  std::string printfPlaceholder = "%";
  printfPlaceholder += spec;
  if (!HasPrintfConversion(spec)) {
    printfPlaceholder += DefaultConversion(valueType);
  }

  format.replace(open, close - open + 1, printfPlaceholder);
  return format;
}

template <typename T>
UIElement MakeValueLabel(std::string label, T value, ValueType valueType,
                         std::string format) {
  return CreateElement(
      [label = std::move(label), value,
       format = NormalizeFormat(std::move(format), valueType)] {
        std::array<char, 128> valueText{};

        if constexpr (std::is_integral_v<T>) {
          std::snprintf(valueText.data(), valueText.size(), format.c_str(),
                        value);
        } else {
          std::snprintf(valueText.data(), valueText.size(), format.c_str(),
                        static_cast<double>(value));
        }

        std::string line = label;
        line += ": ";
        line += valueText.data();
        ImGui::TextUnformatted(line.c_str());
      });
}
} // namespace

UIElement ValueLabel(std::string label, double value, std::string format) {
  return MakeValueLabel(std::move(label), value, ValueType::FloatingPoint,
                        std::move(format));
}

UIElement ValueLabel(std::string label, float value, std::string format) {
  return MakeValueLabel(std::move(label), value, ValueType::FloatingPoint,
                        std::move(format));
}

UIElement ValueLabel(std::string label, int value, std::string format) {
  return MakeValueLabel(std::move(label), value, ValueType::Integer,
                        std::move(format));
}
} // namespace FlightUI
