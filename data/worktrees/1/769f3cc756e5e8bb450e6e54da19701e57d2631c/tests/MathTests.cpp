#include "common/math/Math.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
constexpr double Tolerance = 1.0e-12;

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireNear(double actual, double expected, const std::string &message) {
  if (std::abs(actual - expected) > Tolerance) {
    throw std::runtime_error(message);
  }
}

void TestAngleConversion() {
  RequireNear(math::DegToRad(180.0),
      std::numbers::pi_v<double>,
      "Degree-to-radian conversion failed");
  RequireNear(math::RadToDeg(std::numbers::pi_v<double>),
      180.0,
      "Radian-to-degree conversion failed");
}

void TestAngleWrapping() {
  const double pi = std::numbers::pi_v<double>;
  const double twoPi = 2.0 * pi;
  constexpr double Offset = 1.0e-6;

  RequireNear(math::WrapAngleRad(0.0), 0.0, "Zero radians did not wrap");
  RequireNear(math::WrapAngleRad(pi), pi, "+pi did not wrap to +pi");
  RequireNear(math::WrapAngleRad(-pi), -pi, "-pi did not wrap to -pi");
  RequireNear(math::WrapAngleRad(twoPi), 0.0, "+2pi did not wrap to zero");
  RequireNear(math::WrapAngleRad(-twoPi), 0.0, "-2pi did not wrap to zero");
  RequireNear(math::WrapAngleRad(pi + Offset),
      -pi + Offset,
      "Angle above +pi wrapped incorrectly");
  RequireNear(math::WrapAngleDeg(180.0),
      180.0,
      "+180 degrees did not wrap to +180");
  RequireNear(math::WrapAngleDeg(-180.0),
      -180.0,
      "-180 degrees did not wrap to -180");
  RequireNear(math::WrapAngleDeg(360.0),
      0.0,
      "+360 degrees did not wrap to zero");

  RequireNear(math::DeltaAngleDeg(179.0, -179.0),
      2.0,
      "Positive dateline angle delta was incorrect");
  RequireNear(math::DeltaAngleDeg(-179.0, 179.0),
      -2.0,
      "Negative dateline angle delta was incorrect");
  RequireNear(math::DeltaAngleRad(0.75 * pi, -0.75 * pi),
      0.5 * pi,
      "Radian angle delta was incorrect");
}

void TestGenericWrap() {
  RequireNear(math::Wrap(25.0, 0.0, 360.0),
      25.0,
      "In-range value changed while wrapping");
  RequireNear(math::Wrap(370.0, 0.0, 360.0),
      10.0,
      "Value above range wrapped incorrectly");
  RequireNear(math::Wrap(-10.0, 0.0, 360.0),
      350.0,
      "Value below range wrapped incorrectly");
  RequireNear(math::Wrap(1090.0, 0.0, 360.0),
      10.0,
      "Positive multi-period value wrapped incorrectly");
  RequireNear(math::Wrap(-730.0, 0.0, 360.0),
      350.0,
      "Negative multi-period value wrapped incorrectly");

  bool rejectedInvalidRange = false;
  try {
    (void)math::Wrap(1.0, 2.0, 2.0);
  } catch (const std::invalid_argument &) {
    rejectedInvalidRange = true;
  }
  Require(rejectedInvalidRange, "Wrap accepted an empty range");
}

void TestInterpolation() {
  RequireNear(math::Lerp(2.0, 6.0, 0.0), 2.0, "Lerp start failed");
  RequireNear(math::Lerp(2.0, 6.0, 1.0), 6.0, "Lerp end failed");
  RequireNear(math::Lerp(2.0, 6.0, 0.5), 4.0, "Lerp midpoint failed");
  RequireNear(math::Lerp(2.0, 6.0, 1.5), 8.0, "Lerp extrapolation failed");

  RequireNear(math::InverseLerp(2.0, 6.0, 2.0),
      0.0,
      "InverseLerp start failed");
  RequireNear(math::InverseLerp(2.0, 6.0, 6.0), 1.0, "InverseLerp end failed");
  RequireNear(math::InverseLerp(2.0, 6.0, 4.0),
      0.5,
      "InverseLerp midpoint failed");
  RequireNear(math::InverseLerp(2.0, 6.0, 8.0),
      1.5,
      "InverseLerp extrapolation failed");
  RequireNear(math::InverseLerp(3.0, 3.0, 7.0),
      0.0,
      "Degenerate InverseLerp failed");
}

void TestMoveTowards() {
  RequireNear(math::MoveTowards(1.0, 5.0, 2.0),
      3.0,
      "MoveTowards positive step failed");
  RequireNear(math::MoveTowards(5.0, 1.0, 2.0),
      3.0,
      "MoveTowards negative step failed");
  RequireNear(math::MoveTowards(1.0, 2.0, 5.0),
      2.0,
      "MoveTowards overshot its target");

  bool rejectedNegativeDelta = false;
  try {
    (void)math::MoveTowards(1.0, 2.0, -1.0);
  } catch (const std::invalid_argument &) {
    rejectedNegativeDelta = true;
  }
  Require(rejectedNegativeDelta, "MoveTowards accepted a negative maxDelta");
}

void TestApproximately() {
  Require(math::Approximately(2.0, 2.0),
      "Approximately rejected identical values");
  Require(math::Approximately(2.0, 2.0 + 1.0e-10),
      "Approximately rejected a small difference");
  Require(!math::Approximately(2.0, 2.01),
      "Approximately accepted clearly different values");
  Require(math::Approximately(1.0e9, 1.0e9 + 0.5),
      "Approximately did not apply relative tolerance");
}
} // namespace

int main() {
  try {
    TestAngleConversion();
    TestAngleWrapping();
    TestGenericWrap();
    TestInterpolation();
    TestMoveTowards();
    TestApproximately();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
