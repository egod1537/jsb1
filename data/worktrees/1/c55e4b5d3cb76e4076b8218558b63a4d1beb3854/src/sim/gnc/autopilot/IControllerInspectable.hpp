#pragma once

#include "sim/gnc/Controller.hpp"

#include <type_traits>
#include <typeinfo>

namespace gnc {
class IControllerInspectable {
public:
  virtual ~IControllerInspectable() = default;

  template <typename T> T *GetController() {
    static_assert(std::is_base_of_v<Controller, T>,
        "T must inherit from gnc::Controller");
    return static_cast<T *>(FindController(typeid(T)));
  }

  template <typename T> const T *GetController() const {
    static_assert(std::is_base_of_v<Controller, T>,
        "T must inherit from gnc::Controller");
    return static_cast<const T *>(FindController(typeid(T)));
  }

protected:
  virtual Controller *FindController(const std::type_info &type) = 0;
  virtual const Controller *FindController(
      const std::type_info &type) const = 0;
};
} // namespace gnc
