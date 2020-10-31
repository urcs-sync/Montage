#ifndef PERSIST_STRUCTS_HPP
#define PERSIST_STRUCTS_HPP

// TODO: this may not be a good file name,
// as some structures are actually transient.

#include <atomic>
#include <cassert>
#include <immintrin.h>
#include <ralloc.hpp>

#include "Persistent.hpp"
#include "common_macros.hpp"

namespace pds{
    struct OldSeeNewException : public std::exception {
        const char * what () const throw () {
            return "OldSeeNewException not handled.";
        }
    };

    enum PBlkType {INIT, ALLOC, UPDATE, DELETE, RECLAIMED, EPOCH, OWNED};

    class EpochSys;

    ////////////////////////////
    // PBlk-related structurs //
    ////////////////////////////

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

    struct EpochVerifyException : public std::exception {
        const char * what () const throw () {
            return "Epoch in which operation wants to linearize has passed; retry required.";
        }
    };

    struct sc_desc_t;
    template <class T>
    class atomic_nbptr_t;
    class nbptr_t{
        template <class T>
        friend class atomic_nbptr_t;
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
        nbptr_t(uint64_t v, uint64_t c) : val(v), cnt(c) {};
        nbptr_t() : nbptr_t(0, 0) {};

        inline bool operator==(const nbptr_t & b) const{
            return val==b.val && cnt==b.cnt;
        }
        inline bool operator!=(const nbptr_t & b) const{
            return !operator==(b);
        }
    }__attribute__((aligned(16)));

    extern EpochSys* esys;

    template <class T = uint64_t>
    class atomic_nbptr_t{
        static_assert(sizeof(T) == sizeof(uint64_t), "sizes do not match");
    public:
        // for cnt in nbptr:
        // desc: ....01
        // real val: ....00
        std::atomic<nbptr_t> nbptr;
        nbptr_t load();
        nbptr_t load_verify();
        inline T load_val(){
            return reinterpret_cast<T>(load().val);
        }
        bool CAS_verify(nbptr_t expected, const T& desired);
        inline bool CAS_verify(nbptr_t expected, const nbptr_t& desired){
            return CAS_verify(expected,desired.get_val<T>());
        }
        // CAS doesn't check epoch nor cnt
        bool CAS(nbptr_t expected, const T& desired);
        inline bool CAS(nbptr_t expected, const nbptr_t& desired){
            return CAS(expected,desired.get_val<T>());
        }
        void store(const T& desired);
        inline void store(const nbptr_t& desired){
            store(desired.get_val<T>());
        }
        atomic_nbptr_t(const T& v) : nbptr(nbptr_t(reinterpret_cast<uint64_t>(v), 0)){};
        atomic_nbptr_t() : atomic_nbptr_t(T()){};
    };

    struct sc_desc_t{
    private:
        // for cnt in nbptr:
        // in progress: ....01
        // committed: ....10 
        // aborted: ....11
        std::atomic<nbptr_t> nbptr;
        const uint64_t old_val;
        const uint64_t new_val;
        const uint64_t cas_epoch;
        inline bool abort(nbptr_t _d){
            // bring cnt from ..01 to ..11
            nbptr_t expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
            nbptr_t desired(expected);
            desired.cnt += 2;
            return nbptr.compare_exchange_strong(expected, desired);
        }
        inline bool commit(nbptr_t _d){
            // bring cnt from ..01 to ..10
            nbptr_t expected (_d.val, (_d.cnt & ~0x3UL) | 1UL); // in progress
            nbptr_t desired(expected);
            desired.cnt += 1;
            return nbptr.compare_exchange_strong(expected, desired);
        }
        inline bool committed(nbptr_t _d) const {
            return (_d.cnt & 0x3UL) == 2UL;
        }
        inline bool in_progress(nbptr_t _d) const {
            return (_d.cnt & 0x3UL) == 1UL;
        }
        inline bool match(nbptr_t old_d, nbptr_t new_d) const {
            return ((old_d.cnt & ~0x3UL) == (new_d.cnt & ~0x3UL)) && 
                (old_d.val == new_d.val);
        }
        void cleanup(nbptr_t old_d){
            // must be called after desc is aborted or committed
            nbptr_t new_d = nbptr.load();
            if(!match(old_d,new_d)) return;
            assert(!in_progress(new_d));
            nbptr_t expected(reinterpret_cast<uint64_t>(this),(new_d.cnt & ~0x3UL) | 1UL);
            if(committed(new_d)) {
                // bring cnt from ..10 to ..00
                reinterpret_cast<atomic_nbptr_t<>*>(
                    new_d.val)->nbptr.compare_exchange_strong(
                    expected, 
                    nbptr_t(new_val,new_d.cnt + 2));
            } else {
                //aborted
                // bring cnt from ..11 to ..00
                reinterpret_cast<atomic_nbptr_t<>*>(
                    new_d.val)->nbptr.compare_exchange_strong(
                    expected, 
                    nbptr_t(old_val,new_d.cnt + 1));
            }
        }
    public:
        inline bool committed() const {
            return committed(nbptr.load());
        }
        inline bool in_progress() const {
            return in_progress(nbptr.load());
        }
        // TODO: try_complete used to be inline. Try to make it inline again when refactoring is finished.
        // Hs: consider moving this into EpochSys if having trouble templatizing.
        void try_complete(EpochSys* esys, uint64_t addr);
        sc_desc_t( uint64_t c, uint64_t a, uint64_t o, 
                    uint64_t n, uint64_t e) : 
            nbptr(nbptr_t(a,c)), old_val(o), new_val(n), cas_epoch(e){};
        sc_desc_t() : sc_desc_t(0,0,0,0,0){};
    };

    template<typename T>
    void atomic_nbptr_t<T>::store(const T& desired){
        // this function must be used only when there's no data race
        nbptr_t r = nbptr.load();
        nbptr_t new_r(reinterpret_cast<uint64_t>(desired),r.cnt);
        nbptr.store(new_r);
    }




}

#endif