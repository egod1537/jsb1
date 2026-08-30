#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace FlightUI {
enum class DataType {
  None,
  Double,
  Float,
};

class DataView {
public:
  // Construction
  DataView();
  DataView(const double *data, std::size_t count);
  DataView(const float *data, std::size_t count);
  DataView(const double *data, std::size_t count, std::size_t stride);
  DataView(const float *data, std::size_t count, std::size_t stride);

  // Container views
  static DataView From(const std::vector<double> &values);
  static DataView From(const std::vector<float> &values);
  static DataView From(std::span<const double> values);
  static DataView From(std::span<const float> values);
  static DataView From(std::vector<double> &&values) = delete;
  static DataView From(std::vector<float> &&values) = delete;

  // View metadata
  const void *GetData() const;
  std::size_t GetCount() const;
  std::size_t GetStride() const;
  DataType GetType() const;

private:
  // Non-owning view
  const void *m_Data;
  std::size_t m_Count;
  std::size_t m_Stride;
  DataType m_Type;
};
} // namespace FlightUI
