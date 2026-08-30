#pragma once

#include <cmath>

namespace viz {
struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

inline Vec3 operator+(Vec3 left, Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

inline Vec3 operator-(Vec3 left, Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

inline Vec3 operator*(Vec3 value, float scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

inline Vec3 operator*(Vec3 left, Vec3 right) {
  return {left.x * right.x, left.y * right.y, left.z * right.z};
}

inline float Dot(Vec3 left, Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline Vec3 Cross(Vec3 left, Vec3 right) {
  return {
      left.y * right.z - left.z * right.y,
      left.z * right.x - left.x * right.z,
      left.x * right.y - left.y * right.x,
  };
}

inline Vec3 Normalize(Vec3 value) {
  const float length = std::sqrt(Dot(value, value));
  if (length <= 0.0001F) {
    return {};
  }

  return value * (1.0F / length);
}

inline Vec3 RotateX(Vec3 value, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return {value.x, c * value.y - s * value.z, s * value.y + c * value.z};
}

inline Vec3 RotateY(Vec3 value, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return {c * value.x + s * value.z, value.y, -s * value.x + c * value.z};
}

inline Vec3 RotateZ(Vec3 value, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return {c * value.x - s * value.y, s * value.x + c * value.y, value.z};
}
} // namespace viz
