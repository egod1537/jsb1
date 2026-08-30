#pragma once

#include "flightui/plot/DataView.hpp"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace ds {
template <typename T> class RingBuffer {
public:
  explicit RingBuffer(std::size_t capacity = 0) { reserve(capacity); }

  // Capacity and mutation
  void reserve(std::size_t capacity) {
    std::vector<T> ordered = to_vector();
    if (ordered.size() > capacity) {
      ordered.erase(ordered.begin(), ordered.end() - capacity);
    }

    m_Capacity = capacity;
    m_Values = std::move(ordered);
    m_Values.reserve(m_Capacity);
    m_Offset = 0;
  }

  void clear() {
    m_Values.clear();
    m_Offset = 0;
  }

  void push_back(const T &value) { push(value); }
  void push_back(T &&value) { push(std::move(value)); }

  // Status
  std::size_t capacity() const { return m_Capacity; }
  std::size_t size() const { return m_Values.size(); }
  int offset() const {
    return static_cast<int>(
        std::min(m_Offset, static_cast<std::size_t>(INT_MAX)));
  }

  bool empty() const { return m_Values.empty(); }
  bool full() const { return m_Capacity > 0 && m_Values.size() == m_Capacity; }

  // Storage views
  const T *data() const { return m_Values.data(); }
  T *data() { return m_Values.data(); }

  std::span<const T> span() const { return {data(), size()}; }
  std::span<T> span() { return {data(), size()}; }

  // Plot integration
  FlightUI::DataView data_view() const
    requires(std::is_same_v<T, double> || std::is_same_v<T, float>)
  {
    return FlightUI::DataView(data(), size());
  }

  // Logical indexing
  const T &operator[](std::size_t logicalIndex) const {
    return m_Values[physical_index(logicalIndex)];
  }

  T &operator[](std::size_t logicalIndex) {
    return m_Values[physical_index(logicalIndex)];
  }

  // Ordered copy
  std::vector<T> to_vector() const {
    std::vector<T> ordered;
    ordered.reserve(size());
    for (std::size_t i = 0; i < size(); ++i) {
      ordered.push_back((*this)[i]);
    }
    return ordered;
  }

private:
  // Storage management
  template <typename U> void push(U &&value) {
    if (m_Capacity == 0) {
      return;
    }

    if (m_Values.size() < m_Capacity) {
      m_Values.push_back(std::forward<U>(value));
      return;
    }

    m_Values[m_Offset] = std::forward<U>(value);
    m_Offset = (m_Offset + 1) % m_Capacity;
  }

  std::size_t physical_index(std::size_t logicalIndex) const {
    assert(logicalIndex < m_Values.size());
    if (m_Values.size() < m_Capacity) {
      return logicalIndex;
    }
    return (m_Offset + logicalIndex) % m_Values.size();
  }

  // Ring storage
  std::vector<T> m_Values;
  std::size_t m_Capacity = 0;
  std::size_t m_Offset = 0;
};
} // namespace ds
