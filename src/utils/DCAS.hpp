/*
 * Macro VISIBLE_READ determines which version of API will be used.
 * Macro USE_TSX determines whether TSX (Intel HTM) will be used.
 * 
 * We highly recommend you to use default invisible read version,
 * since it doesn't need you to handle EpochVerifyException and you
 * can call just load rather than load_verify throughout your program
 * 
 * We provides following API for nonblocking data structures to use: 
 * 
 *  atomic_dword_t<T=uint64_t>: atomic double word for storing pointers
 *  that point to nodes, which link payloads in. It contains following
 *  functions:
 * 
 *      store(T val): 
 *          store 64-bit long data without sync; cnt doesn't increment
 * 
 *      store(dword_t d): store(d.val)
 * 
 *      dword_t load(): 
 *          load dword without verifying epoch
 * 
 *      dword_t load_verify(): 
 *          load dword and verify epoch, used as lin point; 
 *          for invisible reads this won't verify epoch
 * 
 *      bool CAS(dword_t expected, T desired): 
 *          CAS in desired value and increment cnt if expected 
 *          matches current dword
 * 
 *      bool CAS_verify(dword_t expected, T desired): 
 *          CAS in desired value and increment cnt if expected 
 *          matches current dword and global epoch doesn't change
 *          since BEGIN_OP
 */

#ifndef DCAS_HPP
#define DCAS_HPP
#include <atomic>

#include <cassert>

// #include "rtm.hpp"
#include <immintrin.h>
#include "ConcurrentPrimitives.hpp"
#include "EpochSys.hpp"
namespace pds{

struct EpochVerifyException : public std::exception {
   const char * what () const throw () {
      return "Epoch in which operation wants to linearize has passed; retry required.";
   }
};

struct sc_desc_t;
template <class T>
class atomic_dword_t;
class dword_t{
    template <class T>
    friend class atomic_dword_t;
    inline bool is_desc() const {
        return (cnt & 3UL) == 1UL;
    }
    inline sc_desc_t* get_desc() const {
        assert(is_desc());
        return reinterpret_cast<sc_desc_t*>(val);
    }
public:
    uint64_t val;
    uint64_t cnt;
    template <typename T=uint64_t>
    inline T get_val() const {
        static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
        return reinterpret_cast<T>(val);
    }
    dword_t(uint64_t v, uint64_t c) : val(v), cnt(c) {};
    dword_t() : dword_t(0, 0) {};

    inline bool operator==(const dword_t & b) const{
        return val==b.val && cnt==b.cnt;
    }
    inline bool operator!=(const dword_t & b) const{
        return !operator==(b);
    }
}__attribute__((aligned(16)));

extern padded<sc_desc_t>* local_descs;
extern EpochSys* esys;
extern padded<uint64_t>* epochs;

template <class T = uint64_t>
class atomic_dword_t{
    static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
public:
    // for cnt in dword:
    // desc: ....01
    // real val: ....00
    std::atomic<dword_t> dword;
    dword_t load();
    dword_t load_verify();
    inline T load_val(){
        return reinterpret_cast<T>(load().val);
    }
    bool CAS_verify(dword_t expected, const T& desired);
    inline bool CAS_verify(dword_t expected, const dword_t& desired){
        return CAS_verify(expected,desired.get_val<T>());
    }
    // CAS doesn't check epoch nor cnt
    bool CAS(dword_t expected, const T& desired);
    inline bool CAS(dword_t expected, const dword_t& desired){
        return CAS(expected,desired.get_val<T>());
    }
    void store(const T& desired);
    inline void store(const dword_t& desired){
        store(desired.get_val<T>());
    }
    atomic_dword_t(const T& v) : dword(dword_t(reinterpret_cast<uint64_t>(v), 0)){};
    atomic_dword_t() : atomic_dword_t(T()){};
};

struct sc_desc_t{
private:
    // for cnt in dword:
    // in progress: ....01
    // committed: ....10 
    // aborted: ....11
    std::atomic<dword_t> dword;
    const uint64_t old_val;
    const uint64_t new_val;
    const uint64_t cas_epoch;
    inline bool abort(dword_t _d){
        // bring cnt from ..01 to ..11
        dword_t expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
        dword_t desired(expected);
        desired.cnt += 2;
        return dword.compare_exchange_strong(expected, desired);
    }
    inline bool commit(dword_t _d){
        // bring cnt from ..01 to ..10
        dword_t expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
        dword_t desired(expected);
        desired.cnt += 1;
        return dword.compare_exchange_strong(expected, desired);
    }
    inline bool committed(dword_t _d) const {
        return (_d.cnt & 0x3UL) == 2UL;
    }
    inline bool in_progress(dword_t _d) const {
        return (_d.cnt & 0x3UL) == 1UL;
    }
    inline bool match(dword_t old_d, dword_t new_d) const {
        return ((old_d.cnt & ~0x3UL) == (new_d.cnt & ~0x3UL)) && 
               (old_d.val == new_d.val);
    }
    void cleanup(dword_t old_d){
        // must be called after desc is aborted or committed
        dword_t new_d = dword.load();
        if(!match(old_d,new_d)) return;
        assert(!in_progress(new_d));
        dword_t expected(reinterpret_cast<uint64_t>(this),(new_d.cnt & ~0x3UL) | 1UL);
        if(committed(new_d)) {
            // bring cnt from ..10 to ..00
            reinterpret_cast<atomic_dword_t<>*>(
                new_d.val)->dword.compare_exchange_strong(
                expected, 
                dword_t(new_val,new_d.cnt + 2));
        } else {
            //aborted
            // bring cnt from ..11 to ..00
            reinterpret_cast<atomic_dword_t<>*>(
                new_d.val)->dword.compare_exchange_strong(
                expected, 
                dword_t(old_val,new_d.cnt + 1));
        }
    }
public:
    inline bool committed() const {
        return committed(dword.load());
    }
    inline bool in_progress() const {
        return in_progress(dword.load());
    }
    inline void try_complete(EpochSys* esys, uint64_t addr){
        dword_t _d = dword.load();
        int ret = 0;
        if(_d.val!=addr) return;
        if(in_progress(_d)){
            if(esys->check_epoch(cas_epoch)){
                ret = 2;
                ret |= commit(_d);
            } else {
                ret = 4;
                ret |= abort(_d);
            }
        }
        cleanup(_d);
    }
    sc_desc_t( uint64_t c, uint64_t a, uint64_t o, 
                uint64_t n, uint64_t e) : 
        dword(dword_t(a,c)), old_val(o), new_val(n), cas_epoch(e){};
    sc_desc_t() : sc_desc_t(0,0,0,0,0){};
};

template<typename T>
void atomic_dword_t<T>::store(const T& desired){
    // this function must be used only when there's no data race
    dword_t r = dword.load();
    dword_t new_r(reinterpret_cast<uint64_t>(desired),r.cnt);
    dword.store(new_r);
}

#ifdef VISIBLE_READ
// implementation of load and cas for visible reads

template<typename T>
dword_t atomic_dword_t<T>::load(){
    dword_t r;
    while(true){
        r = dword.load();
        dword_t ret(r.val,r.cnt+1);
        if(dword.compare_exchange_strong(r, ret))
            return ret;
    }
}

template<typename T>
dword_t atomic_dword_t<T>::load_verify(){
    assert(epochs[_tid].ui != NULL_EPOCH);
    dword_t r;
    while(true){
        r = dword.load();
        if(esys->check_epoch(epochs[_tid].ui)){
            dword_t ret(r.val,r.cnt+1);
            if(dword.compare_exchange_strong(r, ret)){
                return r;
            }
        } else {
            throw EpochVerifyException();
        }
    }
}

template<typename T>
bool atomic_dword_t<T>::CAS_verify(dword_t expected, const T& desired){
    assert(epochs[_tid].ui != NULL_EPOCH);
    if(esys->check_epoch(epochs[_tid].ui)){
        dword_t new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
        return dword.compare_exchange_strong(expected, new_r);
    } else {
        return false;
    }
}

template<typename T>
bool atomic_dword_t<T>::CAS(dword_t expected, const T& desired){
    dword_t new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
    return dword.compare_exchange_strong(expected, new_r);
}

#else /* !VISIBLE_READ */
/* implementation of load and cas for invisible reads */

template<typename T>
dword_t atomic_dword_t<T>::load(){
    dword_t r;
    do { 
        r = dword.load();
        if(r.is_desc()) {
            sc_desc_t* D = r.get_desc();
            D->try_complete(esys, reinterpret_cast<uint64_t>(this));
        }
    } while(r.is_desc());
    return r;
}

template<typename T>
dword_t atomic_dword_t<T>::load_verify(){
    // invisible read doesn't need to verify epoch even if it's a
    // linearization point
    // this saves users from catching EpochVerifyException
    return load();
}

// extern std::atomic<size_t> abort_cnt;
// extern std::atomic<size_t> total_cnt;

template<typename T>
bool atomic_dword_t<T>::CAS_verify(dword_t expected, const T& desired){
    assert(epochs[_tid].ui != NULL_EPOCH);
    // total_cnt.fetch_add(1);
#ifdef USE_TSX
    unsigned status = _xbegin();
    if (status == _XBEGIN_STARTED) {
        dword_t r = dword.load();
        if(!r.is_desc()){
            if( r.cnt!=expected.cnt ||
                r.val!=expected.val ||
                !esys->check_epoch(epochs[_tid].ui)){
                _xend();
                return false;
            } else {
                dword_t new_r (reinterpret_cast<uint64_t>(desired), r.cnt+4);
                dword.store(new_r);
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
        return false;
    }
#endif
    // txn fails; fall back routine
    // abort_cnt.fetch_add(1);
    dword_t r = dword.load();
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
    new (&local_descs[_tid].ui) sc_desc_t(r.cnt+1, 
                                reinterpret_cast<uint64_t>(this), 
                                expected.val, 
                                reinterpret_cast<uint64_t>(desired), 
                                epochs[_tid].ui);
    dword_t new_r(reinterpret_cast<uint64_t>(&local_descs[_tid].ui), r.cnt+1);
    if(!dword.compare_exchange_strong(r,new_r)){
        return false;
    }
    local_descs[_tid].ui.try_complete(esys, reinterpret_cast<uint64_t>(this));
    if(local_descs[_tid].ui.committed()) return true;
    else return false;
}

template<typename T>
bool atomic_dword_t<T>::CAS(dword_t expected, const T& desired){
    // CAS doesn't check epoch; just cas ptr to desired, with cnt+=4
    assert(!expected.is_desc());
    dword_t new_r(reinterpret_cast<uint64_t>(desired), expected.cnt + 4);
    if(!dword.compare_exchange_strong(expected,new_r)){
        return false;
    }
    return true;
}

#endif /* !VISIBLE_READ */

}
#endif