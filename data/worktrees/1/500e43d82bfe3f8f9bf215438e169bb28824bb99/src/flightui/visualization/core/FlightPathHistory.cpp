#include "flightui/visualization/core/FlightPathHistory.hpp"

#include "common/math/Math.hpp"

#include <cmath>

namespace {
constexpr double EarthRadiusMeters = 6'371'000.0;
constexpr double MinimumSampleDistanceMeters = 5.0;
constexpr double MinimumIntervalSampleMovementMeters = 0.1;
constexpr double MaximumSampleIntervalSec = 1.0;
constexpr double PositionJumpResetDistanceMeters = 50'000.0;
constexpr std::size_t MaximumPointCount = 4096;

double Distance(const viz::FlightPathPoint &left,
    const viz::FlightPathPoint &right) {
  return std::hypot(left.northMeters - right.northMeters,
      left.eastMeters - right.eastMeters);
}
} // namespace

namespace viz {
void FlightPathHistory::Reset() {
  originLatitudeRad_.reset();
  originLongitudeRad_.reset();
  points_.clear();
  currentPoint_.reset();
  lastSimulationTimeSec_.reset();
  lastRecordedTimeSec_.reset();
}

void FlightPathHistory::AddSample(double simulationTimeSec, double latitudeRad,
    double longitudeRad) {
  if (!std::isfinite(simulationTimeSec) || !std::isfinite(latitudeRad)
      || !std::isfinite(longitudeRad)) {
    return;
  }

  if (!originLatitudeRad_.has_value() || !originLongitudeRad_.has_value()
      || (lastSimulationTimeSec_.has_value()
          && simulationTimeSec < *lastSimulationTimeSec_)) {
    Reset();
    Initialize(simulationTimeSec, latitudeRad, longitudeRad);
    return;
  }

  const FlightPathPoint point = Project(latitudeRad, longitudeRad);
  if (currentPoint_.has_value()
      && Distance(point, *currentPoint_) > PositionJumpResetDistanceMeters) {
    Reset();
    Initialize(simulationTimeSec, latitudeRad, longitudeRad);
    return;
  }

  currentPoint_ = point;
  lastSimulationTimeSec_ = simulationTimeSec;

  const bool distanceDue =
      points_.empty()
      || Distance(point, points_.back()) >= MinimumSampleDistanceMeters;
  const bool intervalDue =
      (!lastRecordedTimeSec_.has_value()
          || simulationTimeSec - *lastRecordedTimeSec_
                 >= MaximumSampleIntervalSec)
      && Distance(point, points_.back()) >= MinimumIntervalSampleMovementMeters;
  if (!distanceDue && !intervalDue) {
    return;
  }

  points_.push_back(point);
  lastRecordedTimeSec_ = simulationTimeSec;
  while (points_.size() > MaximumPointCount) {
    points_.pop_front();
  }
}

FlightPathPoint FlightPathHistory::Project(double latitudeRad,
    double longitudeRad) const {
  const double latitudeDelta = latitudeRad - *originLatitudeRad_;
  const double longitudeDelta =
      math::WrapAngleRad(longitudeRad - *originLongitudeRad_);
  return {
      .northMeters = latitudeDelta * EarthRadiusMeters,
      .eastMeters =
          longitudeDelta * std::cos(*originLatitudeRad_) * EarthRadiusMeters,
  };
}

void FlightPathHistory::Initialize(double simulationTimeSec, double latitudeRad,
    double longitudeRad) {
  originLatitudeRad_ = latitudeRad;
  originLongitudeRad_ = longitudeRad;
  currentPoint_ = FlightPathPoint{};
  points_.push_back(*currentPoint_);
  lastSimulationTimeSec_ = simulationTimeSec;
  lastRecordedTimeSec_ = simulationTimeSec;
}
} // namespace viz
