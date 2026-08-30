#pragma once

#include <cstddef>
#include <deque>
#include <optional>

namespace viz {
struct FlightPathPoint {
  double northMeters = 0.0;
  double eastMeters = 0.0;
};

class FlightPathHistory {
public:
  // Path lifecycle and sampling
  void Reset();
  void AddSample(double simulationTimeSec, double latitudeRad,
      double longitudeRad);

  // Recorded path
  const std::deque<FlightPathPoint> &GetPoints() const { return points_; }
  std::optional<FlightPathPoint> GetCurrentPoint() const {
    return currentPoint_;
  }

private:
  // Geographic projection
  FlightPathPoint Project(double latitudeRad, double longitudeRad) const;
  void Initialize(double simulationTimeSec, double latitudeRad,
      double longitudeRad);

  // Geographic origin
  std::optional<double> originLatitudeRad_;
  std::optional<double> originLongitudeRad_;

  // Sampling state
  std::deque<FlightPathPoint> points_;
  std::optional<FlightPathPoint> currentPoint_;
  std::optional<double> lastSimulationTimeSec_;
  std::optional<double> lastRecordedTimeSec_;
};
} // namespace viz
