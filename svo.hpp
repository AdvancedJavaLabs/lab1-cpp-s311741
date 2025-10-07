#pragma once
#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>

// Non-feature-complete small vector. It is more compact than in boost.
// It stores inline as many elements as will fit within the size
// of std::vector, minus four bytes
template<typename T>
class svo_vector {
  using large_vector = std::vector<T>;
  constexpr static size_t max_small
    = std::min(size_t(1), (sizeof(large_vector) - 4) / sizeof(T));

  union repr {
    struct {
      // is_small is LSB of the data pointer in std::vector stored in large_storage.
      // It will always be 0 when std::vector is live, due to allocator alignment.
      // So in the small case, we set it to 1.
      bool is_small: 1;
      unsigned size: 31;
      alignas(T) char storage[sizeof(T) * max_small];
      T elems[max_small];
    } small;
    alignas(large_vector) char large_storage[sizeof(large_vector)];
  } repr;
  static_assert(sizeof(repr) == sizeof(large_vector));

  bool is_small() const {
    return repr.small.is_small;
  }

  T* small_storage() {
    return reinterpret_cast<T*>(repr.small.storage);
  }

  const T* small_storage() const {
    return reinterpret_cast<const T*>(repr.small.storage);
  }

  large_vector& large() {
    return reinterpret_cast<large_vector&>(repr.large_storage);
  }

  const large_vector& large() const {
    return reinterpret_cast<const large_vector&>(repr.large_storage);
  }

public:
  using value_type = T;
  using reference = T&;
  using iterator = T*;

  svo_vector() {
    repr.small.is_small = true;
    repr.small.size = 0;
  }

  ~svo_vector() {
    if (is_small()) {
      std::destroy_n(small_storage(), repr.small.size);
    } else {
      large().~vector();
    }
  }

  svo_vector(svo_vector&&) = delete;
  svo_vector(const svo_vector&) = delete;
  svo_vector& operator=(svo_vector&&) = delete;
  svo_vector& operator=(const svo_vector&) = delete;

  size_t size() const {
    return is_small() ? repr.small.size : large().size();
  }

  template<typename... Args>
  reference emplace_back(Args&&... args) {
    if (!is_small()) {
      return large().emplace_back(std::forward<Args>(args)...);
    }
    if (repr.small.size < max_small) {
      return *new(small_storage() + repr.small.size++)
        value_type(std::forward<Args>(args)...);
    }
    large_vector tmp;
    tmp.reserve(max_small + 1);
    std::move(small_storage(),
              small_storage() + repr.small.size,
              std::back_inserter(tmp));
    tmp.emplace_back(std::forward<Args>(args)...);
    std::destroy_n(small_storage(), repr.small.size);
    new(repr.large_storage) large_vector(std::move(tmp));
    return large().back();
  }

  auto begin(this auto& v) { return v.is_small() ? v.small_storage() : v.large().data(); }
  auto end(this auto& v) { return v.begin() + v.size(); }
};