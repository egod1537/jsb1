#pragma once

#include "sim/gnc/Controller.hpp"

#include <optional>

namespace sim {
class Aircraft;
struct Tick;
} // namespace sim

namespace gnc {
struct ControlContext;

struct CourseHoldSettings {
  double targetCourseRad = 0.0;
  double dampingRatio = 0.7;
  double bandwidthSeparationRatio = 5.0;
  double rollNaturalFrequencyRadPerSec = 1.0;
};

struct CourseHoldDiagnostics {
  double targetCourseRad = 0.0;
  double courseErrorRad = 0.0;
  double integralCourseErrorRadSec = 0.0;
  double commandedRollRad = 0.0;
};

class CourseHoldController final : public Controller {
public:
  // Lifecycle
  void Reset() override;

  // Mode
  bool IsEnabled() const;
  void SetEnabled(bool enabled);

  // Configuration
  const CourseHoldSettings &GetSettings() const;
  void SetSettings(const CourseHoldSettings &settings);

  // Diagnostics
  const CourseHoldDiagnostics &GetDiagnostics() const;

  // Control output
  std::optional<double> OnTick(const sim::Aircraft &aircraft,
      const sim::Tick &tick, const ControlContext &context);

private:
  // Mode and configuration
  bool enabled_ = false;
  CourseHoldSettings settings_;

  // Runtime state
  double integralCourseErrorRadSec_ = 0.0;
  double prevError_ = 0.0;

  // Last control result
  CourseHoldDiagnostics diagnostics_;
};
} // namespace gnc
