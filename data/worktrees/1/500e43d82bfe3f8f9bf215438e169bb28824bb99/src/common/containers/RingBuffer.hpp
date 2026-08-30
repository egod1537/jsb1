#pragma once

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace util {
template <typename T> class RingBuffer {
public:
  explicit RingBuffer(std::size_t capacity) : capacity_(capacity) {
    values_.reserve(capacity_);
  }

  // Capacity and state
  std::size_t GetCapacity() const { return capacity_; }
  std::size_t GetSize() const { return values_.size(); }
  bool IsEmpty() const { return values_.empty(); }
  bool IsFull() const { return capacity_ > 0 && values_.size() == capacity_; }

  // Mutation
  void Push(const T &value) { PushValue(value); }
  void Push(T &&value) { PushValue(std::move(value)); }

  void Clear() {
    values_.clear();
    offset_ = 0;
  }

  // Read-only access in oldest-to-newest order
  const T &operator[](std::size_t logicalIndex) const {
    return values_[GetPhysicalIndex(logicalIndex)];
  }

  const T &Front() const {
    assert(!IsEmpty());
    return (*this)[0];
  }

  const T &Back() const {
    assert(!IsEmpty());
    return (*this)[GetSize() - 1];
  }

  // Physical storage access for zero-copy consumers such as plotters
  const T *GetData() const { return values_.data(); }
  std::size_t GetStorageOffset() const { return offset_; }

private:
  template <typename U> void PushValue(U &&value) {
    if (capacity_ == 0) {
      return;
    }

    if (values_.size() < capacity_) {
      values_.push_back(std::forward<U>(value));
      return;
    }

    values_[offset_] = std::forward<U>(value);
    offset_ = (offset_ + 1) % capacity_;
  }

  std::size_t GetPhysicalIndex(std::size_t logicalIndex) const {
    assert(logicalIndex < values_.size());
    if (values_.size() < capacity_) {
      return logicalIndex;
    }
    return (offset_ + logicalIndex) % capacity_;
  }

  std::vector<T> values_;
  std::size_t capacity_ = 0;
  std::size_t offset_ = 0;
};
} // namespace util
