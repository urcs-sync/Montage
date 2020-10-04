#pragma once
#include <cstdlib>
#include <iostream>
#include <new>
#include "ckpt_alloc.hpp"

template <class T>
class STLAlloc {
public:
    ObjectAlloc *alloc = NULL;
    typedef T value_type;
    typedef std::size_t size_type;
    typedef T& reference;
    typedef const T& const_reference;

    STLAlloc() noexcept {
        this->alloc = NULL;
    }

    STLAlloc(ObjectAlloc *a) noexcept {
        this->alloc = a;
    }

    template <class U> STLAlloc(const STLAlloc<U> &b) noexcept {
        this->alloc = b.alloc;
    }

    T* allocate(std::size_t n) {
        assert(alloc != NULL);
        if (auto p = static_cast<T*>(alloc->alloc(n * sizeof(T)))) return p;
        throw std::bad_alloc();
    }

    void deallocate(T *ptr, std::size_t) noexcept {
        assert(alloc != NULL);
        alloc->dealloc(ptr);
    }
};

template <class T, class U>
constexpr bool operator == (const STLAlloc<T> &a, const STLAlloc<U> &b) noexcept {
    return a.alloc == b.alloc;
}

template <class T, class U>
constexpr bool operator != (const STLAlloc<T> &a, const STLAlloc<U> &b) noexcept {
    return !(a == b);
}
