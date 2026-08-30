#pragma once

#include <optional>
#include <string>

namespace sim {
class ErrorTracker {
public:
  void SetError(std::string message);
  void SetErrorIfEmpty(std::string message);
  void ClearError();

  bool HasError() const;
  const std::optional<std::string> &GetLastError() const;

private:
  std::optional<std::string> lastError_;
};
} // namespace sim
