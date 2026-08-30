#pragma once

#include <string_view>

namespace telemetry::paths {
inline constexpr std::string_view AircraftAeroAlpha = "aircraft/aero/alpha";
inline constexpr std::string_view AircraftAeroBeta = "aircraft/aero/beta";

inline constexpr std::string_view AircraftAttitudeRoll =
    "aircraft/attitude/roll";
inline constexpr std::string_view AircraftAttitudePitch =
    "aircraft/attitude/pitch";
inline constexpr std::string_view AircraftAttitudeHeading =
    "aircraft/attitude/heading";

inline constexpr std::string_view AircraftNavigationCourse =
    "aircraft/navigation/course";

inline constexpr std::string_view AircraftBodyVelocityU =
    "aircraft/body_velocity/u";
inline constexpr std::string_view AircraftBodyVelocityV =
    "aircraft/body_velocity/v";
inline constexpr std::string_view AircraftBodyVelocityW =
    "aircraft/body_velocity/w";

inline constexpr std::string_view AircraftRateP = "aircraft/rates/p";
inline constexpr std::string_view AircraftRateQ = "aircraft/rates/q";
inline constexpr std::string_view AircraftRateR = "aircraft/rates/r";

inline constexpr std::string_view AircraftCalibratedAirspeed =
    "aircraft/airdata/calibrated_airspeed";
inline constexpr std::string_view AircraftTrueAirspeed =
    "aircraft/airdata/true_airspeed";
inline constexpr std::string_view AircraftAltitudeAgl =
    "aircraft/position/altitude_agl";

inline constexpr std::string_view AircraftBodyAccelerationU =
    "aircraft/body_acceleration/u_dot";
inline constexpr std::string_view AircraftBodyAccelerationV =
    "aircraft/body_acceleration/v_dot";
inline constexpr std::string_view AircraftBodyAccelerationW =
    "aircraft/body_acceleration/w_dot";

inline constexpr std::string_view AircraftAngularAccelerationP =
    "aircraft/angular_acceleration/p_dot";
inline constexpr std::string_view AircraftAngularAccelerationQ =
    "aircraft/angular_acceleration/q_dot";
inline constexpr std::string_view AircraftAngularAccelerationR =
    "aircraft/angular_acceleration/r_dot";

inline constexpr std::string_view AircraftControlAileron =
    "aircraft/control/aileron";
inline constexpr std::string_view AircraftControlRudder =
    "aircraft/control/rudder";
} // namespace telemetry::paths
