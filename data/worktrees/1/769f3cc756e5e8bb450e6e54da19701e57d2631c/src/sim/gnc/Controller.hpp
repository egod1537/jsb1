#pragma once

namespace gnc {
class Controller {
public:
  virtual ~Controller() = default;

  virtual void Reset() {}
};
} // namespace gnc
