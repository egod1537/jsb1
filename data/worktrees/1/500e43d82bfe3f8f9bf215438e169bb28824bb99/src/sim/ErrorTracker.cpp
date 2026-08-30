#include "sim/ErrorTracker.hpp"

#include <utility>

namespace sim {
void ErrorTracker::SetError(std::string message) {
  lastError_ = std::move(message);
}

void ErrorTracker::SetErrorIfEmpty(std::string message) {
  if (!lastError_.has_value()) {
    SetError(std::move(message));
  }
}

void ErrorTracker::ClearError() { lastError_.reset(); }

bool ErrorTracker::HasError() const { return lastError_.has_value(); }

const std::optional<std::string> &ErrorTracker::GetLastError() const {
  return lastError_;
}
} // namespace sim
