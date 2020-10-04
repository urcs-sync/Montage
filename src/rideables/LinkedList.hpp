#ifndef LINKEDLIST_HPP
#define LINKEDLIST_HPP

#include "HOHHashTable.hpp"

template<typename K, typename V>
using LinkedList = HOHHashTable<K,V,1>;

template <class T> 
class LinkedListFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new LinkedList<T, T>(gtc);
    }
};

#endif