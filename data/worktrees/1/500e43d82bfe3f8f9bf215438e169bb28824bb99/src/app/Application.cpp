#include "app/Application.hpp"

#include "gui/GUI.hpp"
#include "messaging/SimulationMessageAdapter.hpp"
#include "messaging/SimulationMessageClient.hpp"
#include "sim/runtime/SimulationRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;

Clock::duration ToClockDuration(double seconds) {
  return std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(seconds));
}

Clock::duration ToSimulationInterval(double hz) {
  const double clamped = std::clamp(hz,
      sim::MinimumAutomaticSimulationHz,
      sim::MaximumAutomaticSimulationHz);
  return ToClockDuration(1.0 / clamped);
}
} // namespace

Application::Application(std::unique_ptr<gui::GUI> gui,
    std::unique_ptr<sim::SimulationRuntime> simulationRuntime,
    sim::SimulationConfig simConfig)
    : simulationRuntime_(std::move(simulationRuntime)), gui_(std::move(gui)),
      simConfig_(std::move(simConfig)) {
  if (simulationRuntime_ != nullptr) {
    simulationMessageAdapter_ =
        std::make_unique<application::messaging::SimulationMessageAdapter>(
            messageBus_,
            *simulationRuntime_);
    simulationMessageClient_ =
        std::make_unique<application::SimulationMessageClient>(messageBus_);
  }
}

Application::Application() = default;
Application::~Application() = default;

bool Application::Run(const volatile std::sig_atomic_t &running) {
  if (!Start()) {
    Exit();
    return false;
  }

  const bool succeeded = RunMainLoop(running);
  Exit();
  return succeeded;
}

bool Application::RunMainLoop(const volatile std::sig_atomic_t &running) {
  bool succeeded = true;
  double scheduledSimulationHz = simulationRuntime_->GetAutomaticSimulationHz();
  bool scheduledMaximumSimulationSpeed =
      simulationRuntime_->IsMaximumSimulationSpeedEnabled();
  Clock::duration simulationInterval =
      ToSimulationInterval(scheduledSimulationHz);
  const double guiDt = gui_->GetConfig().GetRenderDT();
  const Clock::duration guiInterval =
      guiDt > 0.0 ? ToClockDuration(guiDt) : simulationInterval;

  auto nextSimulationTick = Clock::now();
  auto nextGUITick = nextSimulationTick;

  while (succeeded && running && !gui_->ShouldClose()) {
    auto now = Clock::now();
    const sim::SimulationStatus status = simulationRuntime_->GetStatus();

    if (status.maximumSimulationSpeedEnabled
        != scheduledMaximumSimulationSpeed) {
      scheduledMaximumSimulationSpeed = status.maximumSimulationSpeedEnabled;
      nextSimulationTick = now + simulationInterval;
    }
    if (status.automaticSimulationHz != scheduledSimulationHz) {
      scheduledSimulationHz = status.automaticSimulationHz;
      simulationInterval = ToSimulationInterval(scheduledSimulationHz);
      if (!status.maximumSimulationSpeedEnabled) {
        nextSimulationTick = now + simulationInterval;
      }
    }

    const bool hasPendingManualTick =
        status.executionState == sim::SimulationExecutionState::Paused
        && status.pendingTickCount > 0;
    const bool runAtMaximumSpeed =
        status.maximumSimulationSpeedEnabled
        && status.executionState == sim::SimulationExecutionState::Running;

    if (hasPendingManualTick) {
      succeeded = TickSimulation();
    } else if (runAtMaximumSpeed) {
      do {
        if (!TickSimulation()) {
          succeeded = false;
          break;
        }
        now = Clock::now();
      } while (running && now < nextGUITick);
    } else if (status.executionState == sim::SimulationExecutionState::Paused) {
      nextSimulationTick = now + simulationInterval;
    } else {
      while (now >= nextSimulationTick) {
        if (!TickSimulation()) {
          succeeded = false;
          break;
        }
        nextSimulationTick += simulationInterval;
        now = Clock::now();
      }
    }

    if (!succeeded) {
      break;
    }
    if (now >= nextGUITick) {
      TickGUI();
      now = Clock::now();
      do {
        nextGUITick += guiInterval;
      } while (nextGUITick <= now);
    }
    if (!runAtMaximumSpeed) {
      std::this_thread::sleep_until(std::min(nextSimulationTick, nextGUITick));
    }
  }

  return succeeded;
}

bool Application::Start() {
  if (gui_ == nullptr || simulationRuntime_ == nullptr
      || simulationMessageAdapter_ == nullptr
      || simulationMessageClient_ == nullptr) {
    std::cerr << "Application requires GUI, simulation runtime, message "
                 "adapter, and message client instances\n";
    return false;
  }
  gui_->SetSimulationMessageClient(simulationMessageClient_.get());
  if (!simulationRuntime_->Initialize(simConfig_)) {
    std::cerr << "Failed to initialize simulation runtime: "
              << simulationRuntime_->GetStatus().lastError << '\n';
    return false;
  }
  simulationMessageAdapter_->PublishState();
  if (!flightGear_.Initialize()) {
    return false;
  }
  if (!gui_->Start()) {
    std::cerr << "Failed to start GUI\n";
    return false;
  }
  return true;
}

bool Application::TickSimulation() {
  if (!simulationRuntime_->Tick()) {
    return false;
  }
  const sim::SimulationSnapshot snapshot = simulationRuntime_->GetSnapshot();
  simulationMessageAdapter_->PublishState();
  flightGear_.Update(snapshot.primary);
  return true;
}

void Application::TickGUI() { gui_->Tick(); }

void Application::Exit() {
  if (gui_ != nullptr) {
    gui_->Exit();
  }
  flightGear_.Shutdown();
  if (simulationRuntime_ != nullptr) {
    simulationRuntime_->Shutdown();
  }
}
