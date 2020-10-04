/*
 This list is based on functional list from https://github.com/BartoszMilewski/Okasaki/blob/master/Queue/List.h
*/

#pragma once

#include <cassert>
#include <memory>
#include <functional>
#include <initializer_list>
// #include <iostream> // debugging

#include <immer/memory_policy.hpp>
#include <immer/nvm_utils.hpp>

// TODO: needs refcounting
namespace immer {
template<class T, typename MemoryPolicy = default_memory_policy>
class list
{
    using memory      = MemoryPolicy;
    using heap        = typename memory::heap::type;
    struct Item
    {

        void* operator new(size_t size) {
            return heap::allocate(size);
        }

        void operator delete(void* data) {
            heap::deallocate(sizeof(Item),data);
        }

        void operator delete(void* data, size_t size) {
            heap::deallocate(sizeof(Item),data);
        }


        Item(T v, const Item* const &tail) : _val(v), _next(tail) {}
        // For debugging only
        // ~Item() { std::cout << "! " << _val << std::endl; }
        T _val;
        const Item* _next;
    };
    friend Item;
    explicit list(const Item* const & items) : _head(items) {}

    public:

    void* operator new(size_t size) {
        return heap::allocate(size);
    }

    void operator delete(void* data) {
        heap::deallocate(sizeof(list),data);
    }

    void operator delete(void* data, size_t size) {
        heap::deallocate(sizeof(list),data);
    }

    // Empty list
    list() : _head(nullptr)  {}
    // Cons
    list(T v, list const & tail) {
        _head = new Item(v, tail._head);
        NVM_PERSIST_NOW(_head, sizeof(Item));
    }

    // From initializer list
    list(std::initializer_list<T> init)
    {
        for (auto it = std::rbegin(init); it != std::rend(init); ++it)
        {
            _head = new Item(*it, _head);
            NVM_PERSIST_NOW(_head, sizeof(Item));
        }
    }

    bool isEmpty() const { return !_head; } // conversion to bool
    T front() const
    {
        assert(!isEmpty());
        return _head->_val;
    }
    list popped_front() const
    {
        assert(!isEmpty());
        return list(_head->_next);
    }

    list* popped_front_ptr() const 
    {
        assert(!isEmpty());
        auto* new_list = new list(std::move(list(_head->_next)));
        NVM_PERSIST_NOW(new_list, sizeof(list));
        return new_list;
    }

    // Additional utilities
    list prepended(T v) const
    {
        return list(v, *this);
    }

    list* prepended_ptr(T v) const 
    {
        auto* new_list = new list(std::move(list(v, *this)));
        NVM_PERSIST_NOW(new_list, sizeof(list));
        return new_list;
    }



    list insertedAt(int i, T v) const
    {
        if (i == 0)
            return prepended(v);
        else {
            assert(!isEmpty());
            return list<T>(front(), popped_front().insertedAt(i - 1, v));
        }
    }

    list* insertedAtPtr(int i, T v) const
    {
        list *return_list;
        if (i == 0)
            return_list = new list(std::move(prepended(v)));
        else {
            assert(!isEmpty());
            return_list = new list(std::move( list<T>(front(), popped_front().insertedAt(i - 1, v))));
        }
        NVM_PERSIST_NOW(return_list, sizeof(list));
        return return_list;
    }

    void delete_head(){
        delete(_head);
    }

  private:
    const Item* _head;
};


template<class T>
list<T> concat(list<T> const & a, list<T> const & b)
{
    if (a.isEmpty())
        return b;
    return list<T>(a.front(), concat(a.popped_front(), b));
}

template<class U, class T, class F>
list<U> fmap(F f, list<T> lst)
{
    static_assert(std::is_convertible<F, std::function<U(T)>>::value, 
                 "fmap requires a function type U(T)");
    if (lst.isEmpty()) 
        return list<U>();
    else
        return list<U>(f(lst.front()), fmap<U>(f, lst.popped_front()));
}

template<class T, class P>
list<T> filter(P p, list<T> lst)
{
    static_assert(std::is_convertible<P, std::function<bool(T)>>::value, 
                 "filter requires a function type bool(T)");
    if (lst.isEmpty())
        return list<T>();
    if (p(lst.front()))
        return list<T>(lst.front(), filter(p, lst.popped_front()));
    else
        return filter(p, lst.popped_front());
}

template<class T, class U, class F>
U foldr(F f, U acc, list<T> lst)
{
    static_assert(std::is_convertible<F, std::function<U(T, U)>>::value, 
                 "foldr requires a function type U(T, U)");
    if (lst.isEmpty())
        return acc;
    else
        return f(lst.front(), foldr(f, acc, lst.popped_front()));
}

template<class T, class U, class F>
U foldl(F f, U acc, list<T> lst)
{
    static_assert(std::is_convertible<F, std::function<U(U, T)>>::value, 
                 "foldl requires a function type U(U, T)");
	//static uint64_t count = 0;
	//count++;
    if (lst.isEmpty()) {
		//std::cout << "foldl count:" << count << std::endl;
		//count = 0;
        return acc;
	}
    else {
        return foldl(f, f(acc, lst.front()), lst.popped_front());
	}
//        return foldl(f, f(acc, lst.front()), lst.popped_front());
}

template<class T, class F>
void forEach(list<T> lst, F f) 
{
    static_assert(std::is_convertible<F, std::function<void(T)>>::value, 
                 "forEach requires a function type void(T)");
    if (!lst.isEmpty()) {
        f(lst.front());
        forEach(lst.popped_front(), f);
    }
}

template<class T>
list<T> reversed(list<T> const & lst)
{
    return foldl([](list<T> const & acc, T v)
    {
        return list<T>(v, acc);
    }, list<T>(), lst);
}


template<class Beg, class End>
auto fromIt(Beg it, End end) -> list<typename Beg::value_type>
{
    typedef typename Beg::value_type T;
    if (it == end)
        return list<T>();
    T item = *it;
    return list<T>(item, fromIt(++it, end));
}

// Pass lst by value not reference!
template<class T>
void print(list<T> lst)
{
    if (lst.isEmpty()) {
        std::cout << std::endl;
    }
    else {
        // std::cout << "(" << lst.front() << ", " << lst.headCount() - 1 << ") ";
        std::cout << "(" << lst.front() << ") ";
        print(lst.popped_front());
    }
}
} // namespace immer
