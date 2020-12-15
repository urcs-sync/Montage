#ifndef RSTACK_HPP
#define RSTACK_HPP

#include "Rideable.hpp"

#include "optional.hpp"

template <class V> class RStack : public virtual Rideable{
public:

    virtual optional<V> pop(int tid)=0;

    virtual void push(V val, int tid)=0;
};

#endif   