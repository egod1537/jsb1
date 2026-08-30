#pragma once

#include "flightui/core/UIElement.hpp"

#include <string>

namespace FlightUI {
UIElement Text(std::string text);
UIElement TextDisabled(std::string text);
UIElement TextWrapped(std::string text);
} // namespace FlightUI
