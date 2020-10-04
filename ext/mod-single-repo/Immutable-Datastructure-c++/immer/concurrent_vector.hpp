#pragma once

#include <mutex> // For std::unique_lock
#include <shared_mutex>

#include <immer/vector.hpp>
#include <immer/nvm_utils.hpp>

namespace immer {

// This concurrent wrapper itself is not persistent,
// it merely takes a persistent vector in ctor.

template <typename T>
class concurrent_vector
{
  public: 
    using size_type = detail::hamts::size_t;
    using reference = const T&;

    concurrent_vector (immer::vector<T> **vector) : vector_ (vector) {}

    void set(size_type index, T v) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto *old_vector = *vector_;
        *vector_ = (*vector_)->set_ptr(index, v);
        // We only need to persist the vector pointer, vector itself is persisted
        // in set_ptr.
        NVM_PERSIST(vector_, 1);
        // Refcounting on or off?
        delete old_vector;
    }

    void push_back(T v) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto *old_vector = *vector_;
        *vector_ = (*vector_)->push_back_ptr(v);
        // We only need to persist the vector pointer, vector itself is persisted
        // in set_ptr.
        NVM_PERSIST(vector_, 1);
        // Refcounting on or off?
        delete old_vector;
    }

    reference operator[] (size_type index) const
    { 
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return (**vector_)[index];
    }

    /*!
     * Returns a `const` reference to the element at position
     * `index`. It throws an `std::out_of_range` exception when @f$
     * index \geq size() @f$.  It does not allocate memory and its
     * complexity is *effectively* @f$ O(1) @f$.
     */
    reference at(size_type index) const
    { 
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return  (*vector_)->at(index);
    }

    size_type size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return (*vector_)->size();
    }

  private:
      mutable std::shared_mutex mutex_;
      immer::vector<T> **vector_;
};

} // namespace immer
