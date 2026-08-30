#include "sim/linearization/AsyncAircraftLinearizer.hpp"

#include "sim/linearization/AircraftLinearizer.hpp"

#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace sim {
class AsyncAircraftLinearizer::Impl {
public:
  Impl() : worker_(&Impl::Run, this) {}

  ~Impl() {
    {
      const std::scoped_lock lock(mutex_);
      stopRequested_ = true;
      pendingRequest_.reset();
    }
    condition_.notify_one();
    worker_.join();
  }

  bool Submit(std::uint64_t generation, const SimulationConfig &config,
      const InitialCondition &initialCondition, FDMState sourceState) {
    const std::scoped_lock lock(mutex_);
    if (stopRequested_ || pendingRequest_ || running_ || completion_) {
      return false;
    }

    pendingRequest_ = Request{
        .generation = generation,
        .config = config,
        .initialCondition = initialCondition,
        .sourceState = std::move(sourceState),
    };
    condition_.notify_one();
    return true;
  }

  bool IsBusy() const {
    const std::scoped_lock lock(mutex_);
    return pendingRequest_.has_value() || running_;
  }

  std::optional<Completion> TakeCompletion() {
    const std::scoped_lock lock(mutex_);
    std::optional<Completion> completion = std::move(completion_);
    completion_.reset();
    return completion;
  }

private:
  struct Request {
    std::uint64_t generation{};
    SimulationConfig config;
    InitialCondition initialCondition;
    FDMState sourceState;
  };

  static bool HasSameConfiguration(const SimulationConfig &left,
      const SimulationConfig &right) {
    return left.aircraftName == right.aircraftName
           && left.simulationHz == right.simulationHz;
  }

  Completion Process(Request request) {
    Completion completion{.generation = request.generation};
    try {
      if (!linearizerConfig_
          || !HasSameConfiguration(*linearizerConfig_, request.config)) {
        auto linearizer = std::make_unique<AircraftLinearizer>();
        if (!linearizer->Initialize(request.config, request.initialCondition)) {
          completion.errorMessage =
              "Failed to initialize asynchronous aircraft linearizer";
          return completion;
        }
        linearizer_ = std::move(linearizer);
        linearizerConfig_ = request.config;
      }

      completion.linearization = linearizer_->Linearize(request.sourceState);
    } catch (const std::exception &exception) {
      completion.errorMessage = exception.what();
    } catch (...) {
      completion.errorMessage =
          "Unknown asynchronous aircraft linearization failure";
    }
    return completion;
  }

  void Run() {
    for (;;) {
      std::optional<Request> request;
      {
        std::unique_lock lock(mutex_);
        condition_.wait(lock,
            [this] { return stopRequested_ || pendingRequest_.has_value(); });
        if (stopRequested_) {
          return;
        }

        request = std::move(pendingRequest_);
        pendingRequest_.reset();
        running_ = true;
      }

      Completion completion = Process(std::move(*request));

      {
        const std::scoped_lock lock(mutex_);
        running_ = false;
        if (!stopRequested_) {
          completion_ = std::move(completion);
        }
      }
    }
  }

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::thread worker_;
  bool stopRequested_ = false;
  bool running_ = false;
  std::optional<Request> pendingRequest_;
  std::optional<Completion> completion_;

  std::unique_ptr<AircraftLinearizer> linearizer_;
  std::optional<SimulationConfig> linearizerConfig_;
};

AsyncAircraftLinearizer::AsyncAircraftLinearizer()
    : impl_(std::make_unique<Impl>()) {}

AsyncAircraftLinearizer::~AsyncAircraftLinearizer() = default;

bool AsyncAircraftLinearizer::Submit(std::uint64_t generation,
    const SimulationConfig &config, const InitialCondition &initialCondition,
    FDMState sourceState) {
  return impl_->Submit(generation,
      config,
      initialCondition,
      std::move(sourceState));
}

bool AsyncAircraftLinearizer::IsBusy() const { return impl_->IsBusy(); }

std::optional<AsyncAircraftLinearizer::Completion>
AsyncAircraftLinearizer::TakeCompletion() {
  return impl_->TakeCompletion();
}
} // namespace sim
