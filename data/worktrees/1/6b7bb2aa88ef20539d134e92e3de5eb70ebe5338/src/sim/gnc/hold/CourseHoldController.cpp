#include "sim/gnc/hold/CourseHoldController.hpp"
#include "sim/Aircraft.hpp"
#include "sim/Tick.hpp"
#include "sim/gnc/ControlContext.hpp"
#include "common/math/Math.hpp"

namespace gnc {
void CourseHoldController::Reset() {
  integralCourseErrorRadSec_ = 0.0;
  prevError_ = 0.0;
  diagnostics_ = {};
}

bool CourseHoldController::IsEnabled() const { return enabled_; }

void CourseHoldController::SetEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }

  enabled_ = enabled;
  if (enabled_) {
    Reset();
  } else {
    diagnostics_ = {};
  }
}

const CourseHoldSettings &CourseHoldController::GetSettings() const {
  return settings_;
}

void CourseHoldController::SetSettings(const CourseHoldSettings &settings) {
  settings_ = settings;
}

const CourseHoldDiagnostics &CourseHoldController::GetDiagnostics() const {
  return diagnostics_;
}

std::optional<double> CourseHoldController::OnTick(
    const sim::Aircraft &aircraft, const sim::Tick &tick,
    const ControlContext &) {
  if (!enabled_) {
    return std::nullopt;
  }

  const auto &prop = aircraft.GetProperties();
  const double vG = prop.GroundSpeed().Mps();
  const double g = prop.GravityMps2();

  const double error =
      math::DeltaAngleRad(prop.Course().Rad(), settings_.targetCourseRad);
  integralCourseErrorRadSec_ += tick.dtSec * (error + prevError_) / 2.0;

  const double courseNaturalFrequencyRadPerSec =
      settings_.rollNaturalFrequencyRadPerSec
      / settings_.bandwidthSeparationRatio;
  const double zeta = settings_.dampingRatio;

  const double k0 = courseNaturalFrequencyRadPerSec * vG / g;
  const double kP = 2 * zeta * k0;
  const double kI = courseNaturalFrequencyRadPerSec * k0;
  const double newRoll = kP * error + kI * integralCourseErrorRadSec_;

  diagnostics_ = {
      .targetCourseRad = settings_.targetCourseRad,
      .courseErrorRad = error,
      .integralCourseErrorRadSec = integralCourseErrorRadSec_,
      .commandedRollRad = newRoll,
  };
  prevError_ = error;
  return newRoll;
}
} // namespace gnc
