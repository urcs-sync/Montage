// NOTE: don't include this file elsewhere!
// this is supposed to be a part of Recoverable.hpp

// TODO: replace `new` operator of T with
// per-heap allocation and placement new.
template <typename T, typename... Types> 
T* pnew(Types... args) 
{ 
    T* ret = new T(args...);
    _esys->register_alloc_pblk(ret);
    return ret;
} 

// add more as needed.