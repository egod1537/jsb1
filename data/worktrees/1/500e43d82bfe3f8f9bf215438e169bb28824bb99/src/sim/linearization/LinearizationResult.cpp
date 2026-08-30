#include "LinearizationResult.hpp"
#include <cstddef>
#include <optional>
#include <string_view>

namespace gnc {
std::optional<std::size_t> LinearizationResult::FindStateIndex(
    std::string_view name) const {
  for (std::size_t i = 0; i < stateNames.size(); ++i) {
    if (stateNames[i] == name) {
      return i;
    }
  }

  return std::nullopt;
}

std::optional<std::size_t> LinearizationResult::FindInputIndex(
    std::string_view name) const {
  for (std::size_t i = 0; i < inputNames.size(); ++i) {
    if (inputNames[i] == name) {
      return i;
    }
  }

  return std::nullopt;
}
} // namespace gnc
