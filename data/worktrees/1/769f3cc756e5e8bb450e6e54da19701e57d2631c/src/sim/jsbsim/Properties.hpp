#pragma once

#include <string>

namespace JSBSim {
class FGFDMExec;
} // namespace JSBSim

namespace sim::jsbsim {
class Properties;

class TimeView {
public:
  TimeView(const Properties &properties, const char *secPath);

  double Sec() const;

private:
  const Properties &properties_;
  const char *secPath_;
};

class MutableTimeView {
public:
  MutableTimeView(Properties &properties, const char *secPath);

  double Sec() const;
  void SetSec(double value) const;

private:
  Properties &properties_;
  const char *secPath_;
};

class DistanceView {
public:
  DistanceView(const Properties &properties, const char *ftPath);

  double Ft() const;

private:
  const Properties &properties_;
  const char *ftPath_;
};

class MutableDistanceView {
public:
  MutableDistanceView(Properties &properties, const char *ftPath);

  double Ft() const;
  void SetFt(double value) const;

private:
  Properties &properties_;
  const char *ftPath_;
};

class AngleView {
public:
  AngleView(const Properties &properties, const char *radPath,
      const char *degPath);

  double Rad() const;
  double Deg() const;

private:
  const Properties &properties_;
  const char *radPath_;
  const char *degPath_;
};

class MutableAngleView {
public:
  MutableAngleView(Properties &properties, const char *radPath,
      const char *degPath);

  double Rad() const;
  double Deg() const;
  void SetRad(double value) const;
  void SetDeg(double value) const;

private:
  Properties &properties_;
  const char *radPath_;
  const char *degPath_;
};

class AngularRateView {
public:
  AngularRateView(const Properties &properties, const char *rateRadPerSecPath,
      const char *dotRadPerSec2Path);

  double RadPerSec() const;
  double DegPerSec() const;
  double DotRadPerSec2() const;
  double DotDegPerSec2() const;

private:
  const Properties &properties_;
  const char *rateRadPerSecPath_;
  const char *dotRadPerSec2Path_;
};

class LinearVelocityView {
public:
  LinearVelocityView(const Properties &properties, const char *velocityFpsPath,
      const char *dotFps2Path);

  double Fps() const;
  double Mps() const;
  double DotFps2() const;
  double DotMps2() const;

private:
  const Properties &properties_;
  const char *velocityFpsPath_;
  const char *dotFps2Path_;
};

class SpeedView {
public:
  SpeedView(const Properties &properties, const char *fpsPath,
      const char *ktsPath);

  double Mps() const;
  double Kts() const;
  double Fps() const;
  double FtPerMin() const;

private:
  const Properties &properties_;
  const char *fpsPath_;
  const char *ktsPath_;
};

class MutableSpeedView {
public:
  MutableSpeedView(Properties &properties, const char *fpsPath,
      const char *ktsPath);

  double Mps() const;
  double Kts() const;
  double Fps() const;
  double FtPerMin() const;
  void SetKts(double value) const;

private:
  Properties &properties_;
  const char *fpsPath_;
  const char *ktsPath_;
};

class Properties {
public:
  explicit Properties(JSBSim::FGFDMExec &fdmExec);

  // Raw property access
  double Get(const std::string &name) const;
  void Set(const std::string &name, double value);

  // Simulation time and position
  MutableTimeView SimTime();
  TimeView SimTime() const;
  MutableDistanceView AltitudeAgl();
  DistanceView AltitudeAgl() const;
  AngleView Latitude() const;
  AngleView Longitude() const;
  DistanceView RadiusToVehicle() const;

  // Air data
  MutableSpeedView CalibratedAirspeed();
  SpeedView CalibratedAirspeed() const;
  SpeedView TrueAirspeed() const;
  SpeedView VerticalSpeed() const;

  // Local navigation velocity
  SpeedView NorthVelocity() const;
  SpeedView EastVelocity() const;
  SpeedView GroundSpeed() const;
  AngleView Course() const;

  // Environment
  double GravityMps2() const;

  // Body velocity
  LinearVelocityView U() const;
  LinearVelocityView V() const;
  LinearVelocityView W() const;

  // Attitude and aerodynamic angles
  MutableAngleView Roll();
  AngleView Roll() const;
  MutableAngleView Pitch();
  AngleView Pitch() const;
  AngleView Alpha() const;
  AngleView Beta() const;

  // Angular rates
  AngularRateView P() const;
  AngularRateView Q() const;
  AngularRateView R() const;

private:
  // JSBSim dependency
  JSBSim::FGFDMExec &fdmExec_;
};
} // namespace sim::jsbsim
