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

#ifndef SRC_TRACE_PROCESSOR_CORE_UTIL_SLAB_H_
#define SRC_TRACE_PROCESSOR_CORE_UTIL_SLAB_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"

namespace perfetto::trace_processor::core {

// A fixed-size array allocated on the heap.
template <typename T>
class Slab {
 public:
  Slab() = default;

  // Movable but not copyable.
  Slab(Slab&& other) noexcept : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }
  Slab& operator=(Slab&& other) noexcept {
    if (this != &other) {
      Free();
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }
  Slab(const Slab&) = delete;
  Slab& operator=(const Slab&) = delete;

  ~Slab() { Free(); }

  static Slab<T> Alloc(uint64_t size) {
    if (size == 0) {
      return Slab<T>();
    }
    return Slab<T>(new T[size], size);
  }

  PERFETTO_ALWAYS_INLINE const T& operator[](uint64_t i) const {
    PERFETTO_CHECK(i < size_);
    return data_[i];
  }

  PERFETTO_ALWAYS_INLINE T& operator[](uint64_t i) {
    PERFETTO_CHECK(i < size_);
    return data_[i];
  }

  PERFETTO_ALWAYS_INLINE const T* data() const { return data_; }
  PERFETTO_ALWAYS_INLINE T* data() { return data_; }

  PERFETTO_ALWAYS_INLINE uint64_t size() const { return size_; }

  PERFETTO_ALWAYS_INLINE T* begin() { return data_; }
  PERFETTO_ALWAYS_INLINE const T* begin() const { return data_; }
  PERFETTO_ALWAYS_INLINE T* end() { return data_ + size_; }
  PERFETTO_ALWAYS_INLINE const T* end() const { return data_ + size_; }

 private:
  Slab(T* data, uint64_t size) : data_(data), size_(size) {}

  void Free() {
    delete[] data_;
    data_ = nullptr;
    size_ = 0;
  }

  T* data_ = nullptr;
  uint64_t size_ = 0;
};

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_UTIL_SLAB_H_
