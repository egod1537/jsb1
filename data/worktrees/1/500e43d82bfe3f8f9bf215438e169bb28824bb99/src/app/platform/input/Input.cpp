#include "app/platform/input/Input.hpp"

#include <array>
#include <cstddef>
#include <optional>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace application {
namespace {
constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);

std::size_t ToIndex(Key key) { return static_cast<std::size_t>(key); }

std::optional<Key> ToKey(char value) {
  switch (value) {
  case 'w':
    return Key::W;
  case 's':
    return Key::S;
  case 'a':
    return Key::A;
  case 'd':
    return Key::D;
  case 'q':
    return Key::Q;
  case 'e':
    return Key::E;
  case 'r':
    return Key::R;
  case 'f':
    return Key::F;
  default:
    return std::nullopt;
  }
}

class KeyboardInput {
public:
  bool Initialize() {
    if (initialized_) {
      return true;
    }

#ifdef _WIN32
    initialized_ = true;
    return true;
#else
    if (tcgetattr(STDIN_FILENO, &originalTerminal_) != 0) {
      return false;
    }

    termios rawTerminal = originalTerminal_;
    rawTerminal.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    rawTerminal.c_cc[VMIN] = 0;
    rawTerminal.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &rawTerminal) != 0) {
      return false;
    }

    initialized_ = true;
    return true;
#endif
  }

  std::optional<Key> Poll() {
    if (!initialized_) {
      return std::nullopt;
    }

    char value = '\0';
#ifdef _WIN32
    if (_kbhit() == 0) {
      return std::nullopt;
    }
    value = static_cast<char>(_getch());
#else
    if (read(STDIN_FILENO, &value, 1) <= 0) {
      return std::nullopt;
    }
#endif
    return ToKey(value);
  }

  void Shutdown() {
#ifndef _WIN32
    if (initialized_) {
      tcsetattr(STDIN_FILENO, TCSANOW, &originalTerminal_);
    }
#endif
    initialized_ = false;
  }

private:
#ifndef _WIN32
  termios originalTerminal_{};
#endif
  bool initialized_ = false;
};

KeyboardInput keyboardInput;
std::array<bool, KeyCount> pressedKeys{};
bool initialized = false;
} // namespace

bool Input::Initialize() {
  if (initialized) {
    return true;
  }

  if (!keyboardInput.Initialize()) {
    return false;
  }

  pressedKeys.fill(false);
  initialized = true;
  return true;
}

void Input::Update() {
  pressedKeys.fill(false);
  if (const std::optional<Key> key = keyboardInput.Poll()) {
    pressedKeys[ToIndex(*key)] = true;
  }
}

void Input::Shutdown() {
  keyboardInput.Shutdown();
  pressedKeys.fill(false);
  initialized = false;
}

bool Input::IsKeyDown(Key key) {
  return initialized && key != Key::Count && pressedKeys[ToIndex(key)];
}

bool Input::IsKeyPressed(Key key) { return IsKeyDown(key); }
} // namespace application
