/*
 * Macro VISIBLE_READ determines which version of API will be used.
 * Macro USE_TSX determines whether TSX (Intel HTM) will be used.
 * 
 * We highly recommend you to use default invisible read version,
 * since it doesn't need you to handle EpochVerifyException and you
 * can call just load rather than load_verify throughout your program
 * 
 * We provides following double-compare-single-swap (DCSS) API for
 * nonblocking data structures to use: 
 * 
 *  atomic_nbptr_t<T=uint64_t>: atomic double word for storing pointers
 *  that point to nodes, which link payloads in. It contains following
 *  functions:
 * 
 *      store(T val): 
 *          store 64-bit long data without sync; cnt doesn't increment
 * 
 *      store(nbptr_t d): store(d.val)
 * 
 *      nbptr_t load(): 
 *          load nbptr without verifying epoch
 * 
 *      nbptr_t load_verify(): 
 *          load nbptr and verify epoch, used as lin point; 
 *          for invisible reads this won't verify epoch
 * 
 *      bool CAS(nbptr_t expected, T desired): 
 *          CAS in desired value and increment cnt if expected 
 *          matches current nbptr
 * 
 *      bool CAS_verify(nbptr_t expected, T desired): 
 *          CAS in desired value and increment cnt if expected 
 *          matches current nbptr and global epoch doesn't change
 *          since BEGIN_OP
 */

#ifndef DCSS_HPP
#define DCSS_HPP
#include <atomic>

#include <cassert>

// #include "rtm.hpp"
#include <immintrin.h>
#include "PersistStructs.hpp"
#include "ConcurrentPrimitives.hpp"
#include "EpochSys.hpp"
namespace pds{

#ifdef VISIBLE_READ
// implementation of load and cas for visible reads

template<typename T>
nbptr_t atomic_nbptr_t<T>::load(){
    nbptr_t r;
    while(true){
        r = nbptr.load();
        nbptr_t ret(r.val,r.cnt+1);
        if(nbptr.compare_exchange_strong(r, ret))
            return ret;
    }
}

template<typename T>
nbptr_t atomic_nbptr_t<T>::load_verify(){
    assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);
    nbptr_t r;
    while(true){
        r = nbptr.load();
        if(esys->check_epoch(esys->epochs[EpochSys::tid].ui)){
            nbptr_t ret(r.val,r.cnt+1);
            if(nbptr.compare_exchange_strong(r, ret)){
                return r;
            }
        } else {
            throw EpochVerifyException();
        }
    }
}

template<typename T>
bool atomic_nbptr_t<T>::CAS_verify(nbptr_t expected, const T& desired){
    assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);
    if(esys->check_epoch(esys->epochs[EpochSys::tid].ui)){
        nbptr_t new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
        return nbptr.compare_exchange_strong(expected, new_r);
    } else {
        return false;
    }
}

template<typename T>
bool atomic_nbptr_t<T>::CAS(nbptr_t expected, const T& desired){
    nbptr_t new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
    return nbptr.compare_exchange_strong(expected, new_r);
}

#else /* !VISIBLE_READ */
/* implementation of load and cas for invisible reads */

template<typename T>
nbptr_t atomic_nbptr_t<T>::load(){
    nbptr_t r;
    do { 
        r = nbptr.load();
        if(r.is_desc()) {
            sc_desc_t* D = r.get_desc();
            D->try_complete(esys, reinterpret_cast<uint64_t>(this));
        }
    } while(r.is_desc());
    return r;
}

template<typename T>
nbptr_t atomic_nbptr_t<T>::load_verify(){
    // invisible read doesn't need to verify epoch even if it's a
    // linearization point
    // this saves users from catching EpochVerifyException
    return load();
}

// extern std::atomic<size_t> abort_cnt;
// extern std::atomic<size_t> total_cnt;

template<typename T>
bool atomic_nbptr_t<T>::CAS_verify(nbptr_t expected, const T& desired){
    assert(esys->epochs[EpochSys::tid].ui != NULL_EPOCH);
    // total_cnt.fetch_add(1);
#ifdef USE_TSX
    unsigned status = _xbegin();
    if (status == _XBEGIN_STARTED) {
        nbptr_t r = nbptr.load();
        if(!r.is_desc()){
            if( r.cnt!=expected.cnt ||
                r.val!=expected.val ||
                !esys->check_epoch(esys->epochs[EpochSys::tid].ui)){
                _xend();
                return false;
            } else {
                nbptr_t new_r (reinterpret_cast<uint64_t>(desired), r.cnt+4);
                nbptr.store(new_r);
                _xend();
                return true;
            }
        } else {
            // we only help complete descriptor, but not retry
            _xend();
            r.get_desc()->try_complete(esys, reinterpret_cast<uint64_t>(this));
            return false;
        }
        // execution won't reach here; program should have returned
        assert(0);
    }
#endif
    // txn fails; fall back routine
    // abort_cnt.fetch_add(1);
    nbptr_t r = nbptr.load();
    if(r.is_desc()){
        sc_desc_t* D = r.get_desc();
        D->try_complete(esys, reinterpret_cast<uint64_t>(this));
        return false;
    } else {
        if( r.cnt!=expected.cnt || 
            r.val!=expected.val) {
            return false;
        }
    }
    // now r.cnt must be ..00, and r.cnt+1 is ..01, which means "nbptr
    // contains a descriptor" and "a descriptor is in progress"
    assert((r.cnt & 3UL) == 0UL);
    new (&esys->local_descs[EpochSys::tid].ui) sc_desc_t(r.cnt+1, 
                                reinterpret_cast<uint64_t>(this), 
                                expected.val, 
                                reinterpret_cast<uint64_t>(desired), 
                                esys->epochs[EpochSys::tid].ui);
    nbptr_t new_r(reinterpret_cast<uint64_t>(&esys->local_descs[EpochSys::tid].ui), r.cnt+1);
    if(!nbptr.compare_exchange_strong(r,new_r)){
        return false;
    }
    esys->local_descs[EpochSys::tid].ui.try_complete(esys, reinterpret_cast<uint64_t>(this));
    if(esys->local_descs[EpochSys::tid].ui.committed()) return true;
    else return false;
}

template<typename T>
bool atomic_nbptr_t<T>::CAS(nbptr_t expected, const T& desired){
    // CAS doesn't check epoch; just cas ptr to desired, with cnt+=4
    assert(!expected.is_desc());
    nbptr_t new_r(reinterpret_cast<uint64_t>(desired), expected.cnt + 4);
    if(!nbptr.compare_exchange_strong(expected,new_r)){
        return false;
    }
    return true;
}

#endif /* !VISIBLE_READ */

}
#endif