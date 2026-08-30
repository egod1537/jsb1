#pragma once

#include <functional>

namespace FlightUI {
struct Vector2 {
  float X = 0.0F;
  float Y = 0.0F;
};

using Action = std::function<void()>;

enum class Key {
  A,
  D,
  E,
  F,
  Q,
  R,
  S,
  W,
};

double GetTime();
bool IsKeyPressed(Key key, bool repeat = true);
bool IsCurrentWindowFocused();
bool WantsTextInput();
} // namespace FlightUI
