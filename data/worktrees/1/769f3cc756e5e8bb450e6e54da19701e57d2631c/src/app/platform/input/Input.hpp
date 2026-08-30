#pragma once

namespace application {
enum class Key {
  W,
  S,
  A,
  D,
  Q,
  E,
  R,
  F,
  Count,
};

class Input {
public:
  static bool Initialize();
  static void Update();
  static void Shutdown();

  static bool IsKeyDown(Key key);
  static bool IsKeyPressed(Key key);
};
} // namespace application
