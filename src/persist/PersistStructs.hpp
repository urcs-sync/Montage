#ifndef PERSIST_STRUCTS_HPP
#define PERSIST_STRUCTS_HPP

#include <atomic>
#include <cassert>
#include <immintrin.h>
#include <ralloc.hpp>

#include "Persistent.hpp"
#include "common_macros.hpp"

class Recoverable;

namespace pds{
    struct OldSeeNewException : public std::exception {
        const char * what () const throw () {
            return "OldSeeNewException not handled.";
        }
    };

    enum PBlkType {INIT, ALLOC, UPDATE, DELETE, RECLAIMED, EPOCH, OWNED};

    class EpochSys;
    

    /////////////////////////////
    // PBlk-related structures //
    /////////////////////////////

    class PBlk : public Persistent{
        friend class EpochSys;
    protected:
        // Wentao: the first word should NOT be any persistent value for
        // epoch-system-level recovery (i.e., epoch), as Ralloc repurposes the first
        // word for block free list, which may interfere with the recovery.
        // Currently we use (transient) "reserved" as the first word. If we decide to
        // remove this field, we need to either prepend another dummy word, or
        // change the block free list in Ralloc.

        // transient.
        void* _reserved;

        uint64_t epoch = NULL_EPOCH;
        PBlkType blktype = INIT;
        uint64_t owner_id = 0; // TODO: make consider abandon this field and use id all the time.
        uint64_t id = 0;
        pptr<PBlk> retire = nullptr;
        // bool persisted = false; // For debug purposes. Might not be needed at the end. 

        // void call_persist(){ // For debug purposes. Might not be needed at the end. 
        //     persist();
        //     persisted = true;
        // }
    public:
        void set_epoch(uint64_t e){
            // only for testing
            epoch=e;
        }
        // id gets inited by EpochSys instance.
        PBlk(): epoch(NULL_EPOCH), blktype(INIT), owner_id(0), retire(nullptr){}
        // id gets inited by EpochSys instance.
        PBlk(const PBlk* owner):
            blktype(OWNED), owner_id(owner->blktype==OWNED? owner->owner_id : owner->id) {}
        PBlk(const PBlk& oth): blktype(oth.blktype==OWNED? OWNED:INIT), owner_id(oth.owner_id), id(oth.id) {}
        inline uint64_t get_id() {return id;}
        virtual pptr<PBlk> get_data() {return nullptr;}
        virtual ~PBlk(){
            // Wentao: we need to zeroize epoch and flush it, avoiding it left after free
            epoch = NULL_EPOCH;
            // persist_func::clwb(&epoch);
        }
    };

    template<typename T>
    class PBlkArray : public PBlk{
        friend class EpochSys;
        size_t size;
        // NOTE: see EpochSys::alloc_pblk_array() for its sementical allocators.
        PBlkArray(): PBlk(){}
        PBlkArray(PBlk* owner) : PBlk(owner), content((T*)((char*)this + sizeof(PBlkArray<T>))){}
    public:
        PBlkArray(const PBlkArray<T>& oth): PBlk(oth), size(oth.size),
            content((T*)((char*)this + sizeof(PBlkArray<T>))){}
        virtual ~PBlkArray(){};
        T* content; //transient ptr
        inline size_t get_size()const{return size;}
    };

    struct Epoch : public PBlk{
        std::atomic<uint64_t> global_epoch;
        void persist(){}
        Epoch(){
            global_epoch.store(NULL_EPOCH, std::memory_order_relaxed);
        }
    };

    ////////////////////////////////////////
    // counted pointer-related structures //
    ////////////////////////////////////////

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
    *  atomic_lin_var<T=uint64_t>: atomic double word for storing pointers
    *  that point to nodes, which link payloads in. It contains following
    *  functions:
    * 
    *      store(T val): 
    *          store 64-bit long data without sync; cnt doesn't increment
    * 
    *      store(lin_var d): store(d.val)
    * 
    *      lin_var load(): 
    *          load var without verifying epoch
    * 
    *      lin_var load_verify(): 
    *          load var and verify epoch, used as lin point; 
    *          for invisible reads this won't verify epoch
    * 
    *      bool CAS(lin_var expected, T desired): 
    *          CAS in desired value and increment cnt if expected 
    *          matches current var
    * 
    *      bool CAS_verify(lin_var expected, T desired): 
    *          CAS in desired value and increment cnt if expected 
    *          matches current var and global epoch doesn't change
    *          since BEGIN_OP
    */

    struct EpochVerifyException : public std::exception {
        const char * what () const throw () {
            return "Epoch in which operation wants to linearize has passed; retry required.";
        }
    };

    struct sc_desc_t;

    template <class T>
    class atomic_lin_var;
    class lin_var{
        template <class T>
        friend class atomic_lin_var;
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
        lin_var(uint64_t v, uint64_t c) : val(v), cnt(c) {};
        lin_var() : lin_var(0, 0) {};

        inline bool operator==(const lin_var & b) const{
            return val==b.val && cnt==b.cnt;
        }
        inline bool operator!=(const lin_var & b) const{
            return !operator==(b);
        }
    }__attribute__((aligned(16)));

    template <class T = uint64_t>
    class atomic_lin_var{
        static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
    public:
        // for cnt in var:
        // desc: ....01
        // real val: ....00
        std::atomic<lin_var> var;
        lin_var load(Recoverable* ds);
        lin_var load_verify(Recoverable* ds);
        inline T load_val(Recoverable* ds){
            return reinterpret_cast<T>(load().val);
        }
        bool CAS_verify(Recoverable* ds, lin_var expected, const T& desired);
        inline bool CAS_verify(lin_var expected, const lin_var& desired){
            return CAS_verify(expected,desired.get_val<T>());
        }
        // CAS doesn't check epoch nor cnt
        bool CAS(lin_var expected, const T& desired);
        inline bool CAS(lin_var expected, const lin_var& desired){
            return CAS(expected,desired.get_val<T>());
        }
        void store(const T& desired);
        inline void store(const lin_var& desired){
            store(desired.get_val<T>());
        }
        atomic_lin_var(const T& v) : var(lin_var(reinterpret_cast<uint64_t>(v), 0)){};
        atomic_lin_var() : atomic_lin_var(T()){};
    };

    struct sc_desc_t{
    private:
        // for cnt in var:
        // in progress: ....01
        // committed: ....10 
        // aborted: ....11
        std::atomic<lin_var> var;
        const uint64_t old_val;
        const uint64_t new_val;
        const uint64_t cas_epoch;
        inline bool abort(lin_var _d){
            // bring cnt from ..01 to ..11
            lin_var expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
            lin_var desired(expected);
            desired.cnt += 2;
            return var.compare_exchange_strong(expected, desired);
        }
        inline bool commit(lin_var _d){
            // bring cnt from ..01 to ..10
            lin_var expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
            lin_var desired(expected);
            desired.cnt += 1;
            return var.compare_exchange_strong(expected, desired);
        }
        inline bool committed(lin_var _d) const {
            return (_d.cnt & 0x3UL) == 2UL;
        }
        inline bool in_progress(lin_var _d) const {
            return (_d.cnt & 0x3UL) == 1UL;
        }
        inline bool match(lin_var old_d, lin_var new_d) const {
            return ((old_d.cnt & ~0x3UL) == (new_d.cnt & ~0x3UL)) && 
                (old_d.val == new_d.val);
        }
        void cleanup(lin_var old_d){
            // must be called after desc is aborted or committed
            lin_var new_d = var.load();
            if(!match(old_d,new_d)) return;
            assert(!in_progress(new_d));
            lin_var expected(reinterpret_cast<uint64_t>(this),(new_d.cnt & ~0x3UL) | 1UL);
            if(committed(new_d)) {
                // bring cnt from ..10 to ..00
                reinterpret_cast<atomic_lin_var<>*>(
                    new_d.val)->var.compare_exchange_strong(
                    expected, 
                    lin_var(new_val,new_d.cnt + 2));
            } else {
                //aborted
                // bring cnt from ..11 to ..00
                reinterpret_cast<atomic_lin_var<>*>(
                    new_d.val)->var.compare_exchange_strong(
                    expected, 
                    lin_var(old_val,new_d.cnt + 1));
            }
        }
    public:
        inline bool committed() const {
            return committed(var.load());
        }
        inline bool in_progress() const {
            return in_progress(var.load());
        }
        // TODO: try_complete used to be inline. Try to make it inline again when refactoring is finished.
        void try_complete(Recoverable* ds, uint64_t addr);
        
        sc_desc_t( uint64_t c, uint64_t a, uint64_t o, 
                    uint64_t n, uint64_t e) : 
            var(lin_var(a,c)), old_val(o), new_val(n), cas_epoch(e){};
        sc_desc_t() : sc_desc_t(0,0,0,0,0){};
    };
}

#endif