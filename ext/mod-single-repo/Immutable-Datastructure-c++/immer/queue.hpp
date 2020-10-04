/*
 This list is based on functional list from https://github.com/BartoszMilewski/Okasaki/blob/master/queue/queue.hpp
*/

#pragma once

#include <cassert>
#include <memory>
#include <immer/detail/list/list.hpp>
#include <immer/memory_policy.hpp>

// Performance problems when used persistently
// When multiple copies have to perform check

namespace immer {
template<class T, typename MemoryPolicy = default_memory_policy>
class queue
{
    using memory      = MemoryPolicy;
    using heap        = typename memory::heap::type;

public:
    void* operator new(size_t size) {
        return heap::allocate(size);
    }

    void operator delete(void* data) {
        heap::deallocate(sizeof(queue),data);
    }

    void operator delete(void* data, size_t size) {
        heap::deallocate(sizeof(queue),data);
    }

    queue(){}
    queue(list<T> const & front, list<T> const & rear)
        :_front(front), _rear(rear) {}
    bool isEmpty() const { return _front.isEmpty(); }
    T front()
    {
        assert(!isEmpty());
        return _front.front();
    }
    queue pop_front() const
    {
        assert(!isEmpty());
        return check(_front.popped_front(), _rear);
    }
    queue push_back(T v) const
    {
        return check(_front, list<T>(v, _rear));
    }
    queue* pop_front_ptr() const
    {
        assert(!isEmpty());
        queue* new_queue = new queue(std::move(check(_front.popped_front(), _rear)));
        NVM_PERSIST_NOW(new_queue, sizeof(_front) + sizeof(_rear));
        return new_queue;
    }
    queue* push_back_ptr(T v) const
    {
        queue* new_queue = new queue(std::move(check(_front, list<T>(std::move(v), _rear))));
        NVM_PERSIST_NOW(new_queue, sizeof(_front) + sizeof(_rear));
        return new_queue;
    }
    void delete_front(){
        // call this after pop_front_ptr and old version is being deleted
        _front.delete_head();
    }
    void printq () const 
    {
        print(_front);
        std::cout << " | ";
        print(_rear);
        std::cout << std::endl;
    }
private:
    static queue check(list<T> const & f, list<T> const & r)
    {
        if (f.isEmpty())
        {
            if (!r.isEmpty())
            return queue(reversed(r), list<T>());
        }
        return queue(f, r);
    }
    list<T> _front;
    list<T> _rear; // reversedd
};
} // namespace immer
