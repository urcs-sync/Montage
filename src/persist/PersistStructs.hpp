#ifndef PERSIST_STRUCTS_HPP
#define PERSIST_STRUCTS_HPP

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

}

#endif