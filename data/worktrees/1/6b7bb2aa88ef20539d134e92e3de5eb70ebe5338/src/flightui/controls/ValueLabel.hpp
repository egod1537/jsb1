#pragma once

#include "flightui/core/UIElement.hpp"

#include <string>

namespace FlightUI {
UIElement ValueLabel(std::string label, double value, std::string format);
UIElement ValueLabel(std::string label, float value, std::string format);
UIElement ValueLabel(std::string label, int value, std::string format);
} // namespace FlightUI
