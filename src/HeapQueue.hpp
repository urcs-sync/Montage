
#ifndef HEAPQUEUE_HPP
#define HEAPQUEUE_HPP

#include <string>
#include "Rideable.hpp"

#include "optional.hpp"

template <class K, class V> class HeapQueue : public virtual Rideable{
public:

    virtual optional<V> dequeue(int tid)=0;

    virtual void enqueue(K key, V val, int tid)=0;
};

#endif   