#include "flightui/visualization/core/FlightPathHistory.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
constexpr double EarthRadiusMeters = 6'371'000.0;
constexpr double ToleranceMeters = 0.01;

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireNear(double actual, double expected, const std::string &message) {
  if (std::abs(actual - expected) > ToleranceMeters) {
    throw std::runtime_error(message);
  }
}

void TestLocalProjectionAndSampling() {
  viz::FlightPathHistory history;
  history.AddSample(0.0, 0.0, 0.0);
  history.AddSample(1.0, 100.0 / EarthRadiusMeters, 0.0);
  history.AddSample(2.0, 100.0 / EarthRadiusMeters, 50.0 / EarthRadiusMeters);

  const auto &points = history.GetPoints();
  Require(points.size() == 3, "Flight path did not record moved samples");
  RequireNear(points.front().northMeters, 0.0, "Path origin north was wrong");
  RequireNear(points.front().eastMeters, 0.0, "Path origin east was wrong");
  RequireNear(points.back().northMeters,
      100.0,
      "North displacement projection was wrong");
  RequireNear(points.back().eastMeters,
      50.0,
      "East displacement projection was wrong");
}

void TestCurrentPositionUpdatesBelowSamplingDistance() {
  viz::FlightPathHistory history;
  history.AddSample(0.0, 0.0, 0.0);
  history.AddSample(0.1, 1.0 / EarthRadiusMeters, 0.0);

  Require(history.GetPoints().size() == 1,
      "Flight path recorded an unnecessarily dense sample");
  const auto current = history.GetCurrentPoint();
  Require(current.has_value(), "Flight path lost its current position");
  RequireNear(current->northMeters,
      1.0,
      "Current position did not update between path samples");
}

void TestLongitudeWrapAtDateline() {
  viz::FlightPathHistory history;
  const double originLongitude = std::numbers::pi_v<double> - 1.0e-6;
  const double nextLongitude = -std::numbers::pi_v<double> + 1.0e-6;
  history.AddSample(0.0, 0.0, originLongitude);
  history.AddSample(1.0, 0.0, nextLongitude);

  const auto current = history.GetCurrentPoint();
  Require(current.has_value(), "Dateline sample did not produce a position");
  RequireNear(current->eastMeters,
      2.0e-6 * EarthRadiusMeters,
      "Dateline crossing projected across the long side of Earth");
}

void TestSimulationTimeResetClearsPath() {
  viz::FlightPathHistory history;
  history.AddSample(10.0, 0.0, 0.0);
  history.AddSample(11.0, 100.0 / EarthRadiusMeters, 0.0);
  history.AddSample(0.0, 0.5, 0.5);

  Require(history.GetPoints().size() == 1,
      "Simulation time reset did not clear the flight path");
  const auto current = history.GetCurrentPoint();
  Require(current.has_value(), "Reset path has no current point");
  RequireNear(current->northMeters, 0.0, "Reset path north origin was wrong");
  RequireNear(current->eastMeters, 0.0, "Reset path east origin was wrong");
}

void TestInvalidSampleIsIgnored() {
  viz::FlightPathHistory history;
  history.AddSample(0.0, 0.0, 0.0);
  history.AddSample(1.0, std::numeric_limits<double>::quiet_NaN(), 0.0);
  Require(history.GetPoints().size() == 1,
      "Invalid geographic sample changed the flight path");
}
} // namespace

int main() {
  try {
    TestLocalProjectionAndSampling();
    TestCurrentPositionUpdatesBelowSamplingDistance();
    TestLongitudeWrapAtDateline();
    TestSimulationTimeResetClearsPath();
    TestInvalidSampleIsIgnored();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
