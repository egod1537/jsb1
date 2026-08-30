#include "sim/jsbsim/Properties.hpp"
#include "common/math/Math.hpp"

#include <FGFDMExec.h>

namespace {
constexpr const char *SimTimeSec = "simulation/sim-time-sec";
constexpr const char *AltitudeAglFt = "position/h-agl-ft";
constexpr const char *LatitudeRad = "position/lat-gc-rad";
constexpr const char *LongitudeRad = "position/long-gc-rad";
constexpr const char *RadiusToVehicleFt = "position/radius-to-vehicle-ft";
constexpr const char *CalibratedAirspeedKts = "velocities/vc-kts";
constexpr const char *TrueAirspeedKts = "velocities/vtrue-kts";
constexpr const char *TrueAirspeedFps = "velocities/vtrue-fps";
constexpr const char *NorthVelocityFps = "velocities/v-north-fps";
constexpr const char *EastVelocityFps = "velocities/v-east-fps";
constexpr const char *GroundSpeedFps = "velocities/vg-fps";
constexpr const char *CourseRad = "flight-path/psi-gt-rad";
constexpr const char *GravityFtPerSec2 = "accelerations/gravity-ft_sec2";
constexpr const char *UFps = "velocities/u-fps";
constexpr const char *VFps = "velocities/v-fps";
constexpr const char *WFps = "velocities/w-fps";
constexpr const char *VerticalSpeedFps = "velocities/h-dot-fps";
constexpr const char *RollRad = "attitude/roll-rad";
constexpr const char *PitchRad = "attitude/pitch-rad";
constexpr const char *AlphaDeg = "aero/alpha-deg";
constexpr const char *BetaDeg = "aero/beta-deg";
constexpr const char *RollRateRadPerSec = "velocities/p-rad_sec";
constexpr const char *PitchRateRadPerSec = "velocities/q-rad_sec";
constexpr const char *YawRateRadPerSec = "velocities/r-rad_sec";
constexpr const char *UDotFtPerSec2 = "accelerations/udot-ft_sec2";
constexpr const char *VDotFtPerSec2 = "accelerations/vdot-ft_sec2";
constexpr const char *WDotFtPerSec2 = "accelerations/wdot-ft_sec2";
constexpr const char *PdotRadPerSec2 = "accelerations/pdot-rad_sec2";
constexpr const char *QdotRadPerSec2 = "accelerations/qdot-rad_sec2";
constexpr const char *RdotRadPerSec2 = "accelerations/rdot-rad_sec2";

constexpr double FeetToMeters = 0.3048;
constexpr double KnotToFeetPerSec = 1.6878098571011957;
double FeetPerSecToMetersPerSec(double value) { return value * FeetToMeters; }
double FeetPerSec2ToMetersPerSec2(double value) { return value * FeetToMeters; }
double FeetPerSecToKts(double value) { return value / KnotToFeetPerSec; }
double KtsToFeetPerSec(double value) { return value * KnotToFeetPerSec; }
} // namespace

namespace sim::jsbsim {
TimeView::TimeView(const Properties &properties, const char *secPath)
    : properties_(properties), secPath_(secPath) {}

double TimeView::Sec() const { return properties_.Get(secPath_); }

MutableTimeView::MutableTimeView(Properties &properties, const char *secPath)
    : properties_(properties), secPath_(secPath) {}

double MutableTimeView::Sec() const { return properties_.Get(secPath_); }

void MutableTimeView::SetSec(double value) const {
  properties_.Set(secPath_, value);
}

DistanceView::DistanceView(const Properties &properties, const char *ftPath)
    : properties_(properties), ftPath_(ftPath) {}

double DistanceView::Ft() const { return properties_.Get(ftPath_); }

MutableDistanceView::MutableDistanceView(Properties &properties,
    const char *ftPath)
    : properties_(properties), ftPath_(ftPath) {}

double MutableDistanceView::Ft() const { return properties_.Get(ftPath_); }

void MutableDistanceView::SetFt(double value) const {
  properties_.Set(ftPath_, value);
}

AngleView::AngleView(const Properties &properties, const char *radPath,
    const char *degPath)
    : properties_(properties), radPath_(radPath), degPath_(degPath) {}

double AngleView::Rad() const {
  if (radPath_ != nullptr) {
    return properties_.Get(radPath_);
  }

  return math::DegToRad(Deg());
}

double AngleView::Deg() const {
  if (degPath_ != nullptr) {
    return properties_.Get(degPath_);
  }

  return math::RadToDeg(Rad());
}

MutableAngleView::MutableAngleView(Properties &properties, const char *radPath,
    const char *degPath)
    : properties_(properties), radPath_(radPath), degPath_(degPath) {}

double MutableAngleView::Rad() const {
  if (radPath_ != nullptr) {
    return properties_.Get(radPath_);
  }

  return math::DegToRad(Deg());
}

double MutableAngleView::Deg() const {
  if (degPath_ != nullptr) {
    return properties_.Get(degPath_);
  }

  return math::RadToDeg(Rad());
}

void MutableAngleView::SetRad(double value) const {
  if (radPath_ != nullptr) {
    properties_.Set(radPath_, value);
    return;
  }

  if (degPath_ != nullptr) {
    properties_.Set(degPath_, math::RadToDeg(value));
  }
}

void MutableAngleView::SetDeg(double value) const {
  if (degPath_ != nullptr) {
    properties_.Set(degPath_, value);
    return;
  }

  if (radPath_ != nullptr) {
    properties_.Set(radPath_, math::DegToRad(value));
  }
}

AngularRateView::AngularRateView(const Properties &properties,
    const char *rateRadPerSecPath, const char *dotRadPerSec2Path)
    : properties_(properties), rateRadPerSecPath_(rateRadPerSecPath),
      dotRadPerSec2Path_(dotRadPerSec2Path) {}

double AngularRateView::RadPerSec() const {
  return properties_.Get(rateRadPerSecPath_);
}

double AngularRateView::DegPerSec() const {
  return math::RadToDeg(RadPerSec());
}

double AngularRateView::DotRadPerSec2() const {
  return properties_.Get(dotRadPerSec2Path_);
}

double AngularRateView::DotDegPerSec2() const {
  return math::RadToDeg(DotRadPerSec2());
}

LinearVelocityView::LinearVelocityView(const Properties &properties,
    const char *velocityFpsPath, const char *dotFps2Path)
    : properties_(properties), velocityFpsPath_(velocityFpsPath),
      dotFps2Path_(dotFps2Path) {}

double LinearVelocityView::Fps() const {
  return properties_.Get(velocityFpsPath_);
}

double LinearVelocityView::Mps() const {
  return FeetPerSecToMetersPerSec(Fps());
}

double LinearVelocityView::DotFps2() const {
  return properties_.Get(dotFps2Path_);
}

double LinearVelocityView::DotMps2() const {
  return FeetPerSec2ToMetersPerSec2(DotFps2());
}

SpeedView::SpeedView(const Properties &properties, const char *fpsPath,
    const char *ktsPath)
    : properties_(properties), fpsPath_(fpsPath), ktsPath_(ktsPath) {}

double SpeedView::Mps() const { return FeetPerSecToMetersPerSec(Fps()); }

double SpeedView::Kts() const {
  if (ktsPath_ != nullptr) {
    return properties_.Get(ktsPath_);
  }

  return FeetPerSecToKts(Fps());
}

double SpeedView::Fps() const {
  if (fpsPath_ != nullptr) {
    return properties_.Get(fpsPath_);
  }

  return KtsToFeetPerSec(Kts());
}

double SpeedView::FtPerMin() const { return Fps() * 60.0; }

MutableSpeedView::MutableSpeedView(Properties &properties, const char *fpsPath,
    const char *ktsPath)
    : properties_(properties), fpsPath_(fpsPath), ktsPath_(ktsPath) {}

double MutableSpeedView::Mps() const { return FeetPerSecToMetersPerSec(Fps()); }

double MutableSpeedView::Kts() const {
  if (ktsPath_ != nullptr) {
    return properties_.Get(ktsPath_);
  }

  return FeetPerSecToKts(Fps());
}

double MutableSpeedView::Fps() const {
  if (fpsPath_ != nullptr) {
    return properties_.Get(fpsPath_);
  }

  return KtsToFeetPerSec(Kts());
}

double MutableSpeedView::FtPerMin() const { return Fps() * 60.0; }

void MutableSpeedView::SetKts(double value) const {
  if (ktsPath_ != nullptr) {
    properties_.Set(ktsPath_, value);
    return;
  }

  if (fpsPath_ != nullptr) {
    properties_.Set(fpsPath_, KtsToFeetPerSec(value));
  }
}

Properties::Properties(JSBSim::FGFDMExec &fdmExec) : fdmExec_(fdmExec) {}

double Properties::Get(const std::string &name) const {
  return fdmExec_.GetPropertyValue(name);
}

void Properties::Set(const std::string &name, double value) {
  fdmExec_.SetPropertyValue(name, value);
}

MutableTimeView Properties::SimTime() {
  return MutableTimeView(*this, SimTimeSec);
}

TimeView Properties::SimTime() const { return TimeView(*this, SimTimeSec); }

MutableDistanceView Properties::AltitudeAgl() {
  return MutableDistanceView(*this, AltitudeAglFt);
}

DistanceView Properties::AltitudeAgl() const {
  return DistanceView(*this, AltitudeAglFt);
}

AngleView Properties::Latitude() const {
  return AngleView(*this, LatitudeRad, nullptr);
}

AngleView Properties::Longitude() const {
  return AngleView(*this, LongitudeRad, nullptr);
}

DistanceView Properties::RadiusToVehicle() const {
  return DistanceView(*this, RadiusToVehicleFt);
}

MutableSpeedView Properties::CalibratedAirspeed() {
  return MutableSpeedView(*this, nullptr, CalibratedAirspeedKts);
}

SpeedView Properties::CalibratedAirspeed() const {
  return SpeedView(*this, nullptr, CalibratedAirspeedKts);
}

SpeedView Properties::TrueAirspeed() const {
  return SpeedView(*this, TrueAirspeedFps, TrueAirspeedKts);
}

SpeedView Properties::VerticalSpeed() const {
  return SpeedView(*this, VerticalSpeedFps, nullptr);
}

SpeedView Properties::NorthVelocity() const {
  return SpeedView(*this, NorthVelocityFps, nullptr);
}

SpeedView Properties::EastVelocity() const {
  return SpeedView(*this, EastVelocityFps, nullptr);
}

SpeedView Properties::GroundSpeed() const {
  return SpeedView(*this, GroundSpeedFps, nullptr);
}

AngleView Properties::Course() const {
  return AngleView(*this, CourseRad, nullptr);
}

double Properties::GravityMps2() const {
  return FeetPerSec2ToMetersPerSec2(Get(GravityFtPerSec2));
}

LinearVelocityView Properties::U() const {
  return LinearVelocityView(*this, UFps, UDotFtPerSec2);
}

LinearVelocityView Properties::V() const {
  return LinearVelocityView(*this, VFps, VDotFtPerSec2);
}

LinearVelocityView Properties::W() const {
  return LinearVelocityView(*this, WFps, WDotFtPerSec2);
}

MutableAngleView Properties::Roll() {
  return MutableAngleView(*this, RollRad, nullptr);
}

AngleView Properties::Roll() const {
  return AngleView(*this, RollRad, nullptr);
}

MutableAngleView Properties::Pitch() {
  return MutableAngleView(*this, PitchRad, nullptr);
}

AngleView Properties::Pitch() const {
  return AngleView(*this, PitchRad, nullptr);
}

AngleView Properties::Alpha() const {
  return AngleView(*this, nullptr, AlphaDeg);
}

AngleView Properties::Beta() const {
  return AngleView(*this, nullptr, BetaDeg);
}

AngularRateView Properties::P() const {
  return AngularRateView(*this, RollRateRadPerSec, PdotRadPerSec2);
}

AngularRateView Properties::Q() const {
  return AngularRateView(*this, PitchRateRadPerSec, QdotRadPerSec2);
}

AngularRateView Properties::R() const {
  return AngularRateView(*this, YawRateRadPerSec, RdotRadPerSec2);
}
} // namespace sim::jsbsim
