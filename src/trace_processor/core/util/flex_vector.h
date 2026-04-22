/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_CORE_UTIL_FLEX_VECTOR_H_
#define SRC_TRACE_PROCESSOR_CORE_UTIL_FLEX_VECTOR_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/core/util/slab.h"

namespace perfetto::trace_processor::core {

// A vector-like container that grows in fixed-size chunks (slabs) and supports
// efficient shrinking from the front.
template <typename T>
class FlexVector {
 public:
  static constexpr uint64_t kCapacityMultiple = 1024;

  FlexVector() = default;

  // Movable but not copyable.
  FlexVector(FlexVector&&) = default;
  FlexVector& operator=(FlexVector&&) = default;
  FlexVector(const FlexVector&) = delete;
  FlexVector& operator=(const FlexVector&) = delete;

  static FlexVector<T> CreateWithSize(uint64_t size) {
    return FlexVector(base::AlignUp(size, kCapacityMultiple), size);
  }

  static FlexVector<T> CreateWithCapacity(uint64_t capacity) {
    return FlexVector(base::AlignUp(capacity, kCapacityMultiple), 0);
  }

  PERFETTO_ALWAYS_INLINE void push_back(T value) {
    if (PERFETTO_UNLIKELY(size_ == capacity())) {
      IncreaseCapacity();
    }
    slab_[size_++] = value;
  }

  void push_back_multiple(T value, uint64_t count) {
    while (PERFETTO_UNLIKELY(size_ + count > capacity())) {
      IncreaseCapacity();
    }
    uint64_t end = size_ + count;
    for (uint64_t i = size_; i < end; ++i) {
      slab_[i] = value;
    }
    size_ = end;
  }

  PERFETTO_ALWAYS_INLINE void pop_back() {
    PERFETTO_DCHECK(size_ > 0);
    --size_;
  }

  PERFETTO_ALWAYS_INLINE const T& operator[](uint64_t i) const {
    PERFETTO_CHECK(i < size_);
    return slab_[i];
  }

  PERFETTO_ALWAYS_INLINE T& operator[](uint64_t i) {
    PERFETTO_CHECK(i < size_);
    return slab_[i];
  }

  PERFETTO_ALWAYS_INLINE const T& back() const {
    PERFETTO_DCHECK(size_ > 0);
    return slab_[size_ - 1];
  }

  PERFETTO_ALWAYS_INLINE T& back() {
    PERFETTO_DCHECK(size_ > 0);
    return slab_[size_ - 1];
  }

  PERFETTO_ALWAYS_INLINE const T& front() const {
    PERFETTO_DCHECK(size_ > 0);
    return slab_[0];
  }

  PERFETTO_ALWAYS_INLINE T& front() {
    PERFETTO_DCHECK(size_ > 0);
    return slab_[0];
  }

  PERFETTO_ALWAYS_INLINE const T* data() const { return slab_.data(); }
  PERFETTO_ALWAYS_INLINE T* data() { return slab_.data(); }

  PERFETTO_ALWAYS_INLINE uint64_t size() const { return size_; }
  PERFETTO_ALWAYS_INLINE uint64_t capacity() const { return slab_.size(); }
  PERFETTO_ALWAYS_INLINE bool empty() const { return size_ == 0; }

  void clear() {
    size_ = 0;
  }

  void resize(uint64_t new_size) {
    if (new_size <= capacity()) {
      size_ = new_size;
      return;
    }
    Slab<T> new_slab = Slab<T>::Alloc(base::AlignUp(new_size, kCapacityMultiple));
    if (size_ > 0) {
      memcpy(new_slab.data(), slab_.data(), size_ * sizeof(T));
    }
    slab_ = std::move(new_slab);
    size_ = new_size;
  }

  void ShrinkFromFront(uint64_t count) {
    if (count == 0) {
      return;
    }
    PERFETTO_CHECK(count <= size_);
    if (count < size_) {
      memmove(slab_.data(), slab_.data() + count, (size_ - count) * sizeof(T));
    } else {
      slab_ = Slab<T>();
    }
    size_ -= count;
  }

  void shrink_to_fit() {
    if (size_ == 0) {
      slab_ = Slab<T>();
    } else {
      Slab<T> new_slab =
          Slab<T>::Alloc(base::AlignUp(size_, kCapacityMultiple));
      memcpy(new_slab.data(), slab_.data(), size_ * sizeof(T));
      slab_ = std::move(new_slab);
    }
  }

  PERFETTO_ALWAYS_INLINE T* begin() { return slab_.data(); }
  PERFETTO_ALWAYS_INLINE const T* begin() const { return slab_.data(); }
  PERFETTO_ALWAYS_INLINE T* end() { return slab_.data() + size_; }
  PERFETTO_ALWAYS_INLINE const T* end() const { return slab_.data() + size_; }

 private:
  FlexVector(uint64_t capacity, uint64_t size)
      : slab_(Slab<T>::Alloc(capacity)), size_(size) {}

  void IncreaseCapacity() {
    uint64_t new_capacity = capacity() + kCapacityMultiple;
    Slab<T> new_slab = Slab<T>::Alloc(new_capacity);
    if (size_ > 0) {
      memcpy(new_slab.data(), slab_.data(), size_ * sizeof(T));
    }
    slab_ = std::move(new_slab);
  }

  Slab<T> slab_;
  uint64_t size_ = 0;
};

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_UTIL_FLEX_VECTOR_H_
