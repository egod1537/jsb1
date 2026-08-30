#include "gui/windows/LinearizationValueTransform.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace {
constexpr double Tolerance = 1.0e-12;

void RequireNear(double actual, double expected) {
  assert(std::abs(actual - expected) <= Tolerance);
}

void TestRawTransformPreservesValues() {
  using gui::LinearizationValueTransform;

  RequireNear(gui::TransformLinearizationValue(-123.5,
                  LinearizationValueTransform::Raw),
      -123.5);
  RequireNear(
      gui::TransformLinearizationValue(0.0, LinearizationValueTransform::Raw),
      0.0);
}

void TestSignedLogTransformPreservesSignAndCompressesMagnitude() {
  using gui::LinearizationValueTransform;

  RequireNear(gui::TransformLinearizationValue(9.0,
                  LinearizationValueTransform::SignedLog10),
      1.0);
  RequireNear(gui::TransformLinearizationValue(-99.0,
                  LinearizationValueTransform::SignedLog10),
      -2.0);
  RequireNear(gui::TransformLinearizationValue(0.0,
                  LinearizationValueTransform::SignedLog10),
      0.0);
}

void TestSignedLogTransformHandlesSpecialValues() {
  using gui::LinearizationValueTransform;
  constexpr auto Transform = LinearizationValueTransform::SignedLog10;

  assert(std::isinf(
      gui::TransformLinearizationValue(std::numeric_limits<double>::infinity(),
          Transform)));
  assert(std::isinf(
      gui::TransformLinearizationValue(-std::numeric_limits<double>::infinity(),
          Transform)));
  assert(std::isnan(
      gui::TransformLinearizationValue(std::numeric_limits<double>::quiet_NaN(),
          Transform)));
}
} // namespace

int main() {
  TestRawTransformPreservesValues();
  TestSignedLogTransformPreservesSignAndCompressesMagnitude();
  TestSignedLogTransformHandlesSpecialValues();
  return 0;
}
