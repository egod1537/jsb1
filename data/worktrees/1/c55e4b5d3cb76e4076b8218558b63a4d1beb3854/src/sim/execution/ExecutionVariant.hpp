#pragma once

#include <array>
#include <string_view>

namespace sim {
enum class ExecutionVariant {
  Baseline,
  Primary,
};

inline constexpr std::array<ExecutionVariant, 2> SupportedExecutionVariants = {
    ExecutionVariant::Baseline,
    ExecutionVariant::Primary,
};

std::string_view ToString(ExecutionVariant variant);
bool TryParseExecutionVariant(std::string_view value,
    ExecutionVariant &variant);
} // namespace sim
