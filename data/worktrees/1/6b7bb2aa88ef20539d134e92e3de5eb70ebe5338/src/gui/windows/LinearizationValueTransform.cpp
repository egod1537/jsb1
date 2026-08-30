#include "gui/windows/LinearizationValueTransform.hpp"

#include <cmath>
#include <numbers>

namespace gui {
double TransformLinearizationValue(double value,
    LinearizationValueTransform transform) {
  switch (transform) {
  case LinearizationValueTransform::Raw:
    return value;
  case LinearizationValueTransform::SignedLog10:
    return std::copysign(std::log1p(std::abs(value)) * std::numbers::log10e,
        value);
  }

  return value;
}
} // namespace gui
