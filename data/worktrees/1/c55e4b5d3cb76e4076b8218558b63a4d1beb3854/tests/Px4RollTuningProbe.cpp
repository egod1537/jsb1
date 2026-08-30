#include "sim/Simulation.hpp"
#include "sim/control/FlightControlManager.hpp"
#include "sim/control/FlightControlMode.hpp"
#include "sim/gnc/autopilot/PX4Autopilot.hpp"
#include "sim/gnc/hold/Px4RollHoldReferenceController.hpp"
#include "common/math/Math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
constexpr double SimulationHz = 120.0;
constexpr double EvaluationDurationSec = 30.0;
constexpr double OscillationWindowSec = 5.0;

struct TuningProfile {
  std::string_view name;
  double timeConstantSec;
  double rateP;
  double rateI;
  double rateD;
  double rateFeedForward;
  double integratorLimit;
};

struct ResponseMetrics {
  double settlingTimeOneDegSec = std::numeric_limits<double>::infinity();
  double settlingTimeHalfDegSec = std::numeric_limits<double>::infinity();
  double finalErrorDeg = 0.0;
  double overshootDeg = 0.0;
  double oscillationRangeDeg = 0.0;
};

control::FlightControlManager &GetFlightControlManager(
    sim::Simulation &simulation) {
  auto *manager = simulation.GetComponent<control::FlightControlManager>();
  if (manager == nullptr) {
    throw std::runtime_error("FlightControlManager is missing");
  }
  return *manager;
}

void ConfigureProfile(sim::Simulation &simulation, const TuningProfile &profile,
    double targetRollRad) {
  auto &manager = GetFlightControlManager(simulation);
  auto &autopilot = dynamic_cast<gnc::PX4Autopilot &>(manager.GetAutopilot());
  auto *px4 = autopilot.GetController<gnc::Px4RollHoldReferenceController>();
  if (px4 == nullptr) {
    throw std::runtime_error("PX4 Roll Hold controller is missing");
  }

  autopilot.SetTargetRollRad(targetRollRad);
  autopilot.SetRollHoldEnabled(true);

  gnc::Px4RollHoldReferenceSettings settings = px4->GetSettings();
  settings.timeConstantSec = profile.timeConstantSec;
  settings.rateProportionalGain = profile.rateP;
  settings.rateIntegralGain = profile.rateI;
  settings.rateDerivativeGain = profile.rateD;
  settings.rateFeedForwardGain = profile.rateFeedForward;
  settings.integratorLimit = profile.integratorLimit;
  px4->SetSettings(settings);

  manager.SetMode(control::FlightControlMode::Autopilot);
}

ResponseMetrics EvaluateProfile(sim::Simulation &simulation,
    const TuningProfile &profile, double targetStepDeg, bool firstScenario) {
  if (!firstScenario && !simulation.Reset()) {
    throw std::runtime_error("Failed to reset simulation for tuning profile");
  }

  auto &aircraft = simulation.GetAircraft();
  const double initialRollRad = aircraft.GetProperties().Roll().Rad();
  const double targetRollRad = initialRollRad + math::DegToRad(targetStepDeg);
  const double stepDirection = targetStepDeg >= 0.0 ? 1.0 : -1.0;
  ConfigureProfile(simulation, profile, targetRollRad);

  const int tickCount = static_cast<int>(EvaluationDurationSec * SimulationHz);
  const int oscillationStartTick = static_cast<int>(
      (EvaluationDurationSec - OscillationWindowSec) * SimulationHz);
  double lastOutsideOneDegSec = 0.0;
  double lastOutsideHalfDegSec = 0.0;
  double maximumProgressRad = 0.0;
  double oscillationMinimumRad = 0.0;
  double oscillationMaximumRad = 0.0;

  for (int tickIndex = 0; tickIndex < tickCount; ++tickIndex) {
    if (!simulation.Tick()) {
      throw std::runtime_error("Simulation failed during tuning probe");
    }

    const double rollRad = aircraft.GetProperties().Roll().Rad();
    const double errorDeg = std::fabs(math::RadToDeg(targetRollRad - rollRad));
    const double timeSec = (tickIndex + 1) / SimulationHz;
    if (errorDeg > 1.0) {
      lastOutsideOneDegSec = timeSec;
    }
    if (errorDeg > 0.5) {
      lastOutsideHalfDegSec = timeSec;
    }
    maximumProgressRad = std::max(maximumProgressRad,
        stepDirection * (rollRad - initialRollRad));

    if (tickIndex == oscillationStartTick) {
      oscillationMinimumRad = rollRad;
      oscillationMaximumRad = rollRad;
    } else if (tickIndex > oscillationStartTick) {
      oscillationMinimumRad = std::min(oscillationMinimumRad, rollRad);
      oscillationMaximumRad = std::max(oscillationMaximumRad, rollRad);
    }
  }

  const double finalRollRad = aircraft.GetProperties().Roll().Rad();
  ResponseMetrics metrics;
  metrics.settlingTimeOneDegSec =
      lastOutsideOneDegSec < EvaluationDurationSec
          ? lastOutsideOneDegSec + 1.0 / SimulationHz
          : std::numeric_limits<double>::infinity();
  metrics.settlingTimeHalfDegSec =
      lastOutsideHalfDegSec < EvaluationDurationSec
          ? lastOutsideHalfDegSec + 1.0 / SimulationHz
          : std::numeric_limits<double>::infinity();
  metrics.finalErrorDeg =
      math::RadToDeg(std::fabs(targetRollRad - finalRollRad));
  metrics.overshootDeg = std::max(0.0,
      math::RadToDeg(maximumProgressRad) - std::fabs(targetStepDeg));
  metrics.oscillationRangeDeg =
      math::RadToDeg(oscillationMaximumRad - oscillationMinimumRad);
  return metrics;
}
} // namespace

int main() {
  try {
    constexpr std::array Profiles{
        TuningProfile{"previous_default", 0.60, 0.060, 0.010, 0.0, 0.40, 0.10},
        TuningProfile{"c172x_tuned", 0.35, 0.160, 0.080, 0.0, 0.80, 0.15},
        TuningProfile{"aggressive", 0.30, 0.200, 0.100, 0.0, 0.90, 0.20},
    };

    sim::Simulation simulation(std::make_unique<gnc::PX4Autopilot>());
    sim::SimulationConfig config;
    config.simulationHz = SimulationHz;
    if (!simulation.Initialize(config)) {
      throw std::runtime_error("Failed to initialize tuning simulation");
    }

    constexpr std::array TargetStepsDeg{10.0, -10.0};
    std::cout
        << "\n[TUNE] profile                 step  settle1  settle0.5  final  "
           "overshoot  osc5\n";
    bool firstScenario = true;
    for (std::size_t index = 0; index < Profiles.size(); ++index) {
      for (const double targetStepDeg : TargetStepsDeg) {
        const ResponseMetrics metrics = EvaluateProfile(simulation,
            Profiles[index],
            targetStepDeg,
            firstScenario);
        firstScenario = false;
        std::cout << "[TUNE] " << std::left << std::setw(23)
                  << Profiles[index].name << std::right << std::fixed
                  << std::setprecision(1) << std::setw(6) << targetStepDeg
                  << std::setprecision(3) << std::setw(9)
                  << metrics.settlingTimeOneDegSec << std::setw(11)
                  << metrics.settlingTimeHalfDegSec << std::setw(8)
                  << metrics.finalErrorDeg << std::setw(11)
                  << metrics.overshootDeg << std::setw(8)
                  << metrics.oscillationRangeDeg << '\n';
      }
    }
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
