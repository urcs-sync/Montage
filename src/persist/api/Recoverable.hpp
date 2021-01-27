#ifndef RECOVERABLE_HPP
#define RECOVERABLE_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
// TODO: report recover errors/exceptions

class Recoverable;

namespace pds{
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
            return reinterpret_cast<T>(load(ds).val);
        }
        bool CAS_verify(Recoverable* ds, lin_var expected, const T& desired);
        inline bool CAS_verify(Recoverable* ds, lin_var expected, const lin_var& desired){
            return CAS_verify(ds, expected,desired.get_val<T>());
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

class Recoverable{
    pds::EpochSys* _esys = nullptr;
    
    // current epoch of each thread.
    padded<uint64_t>* epochs = nullptr;
    // last epoch of each thread, for sync().
    padded<uint64_t>* last_epochs = nullptr;
    // containers for pending allocations
    padded<std::vector<pds::PBlk*>>* pending_allocs = nullptr;
    // local descriptors for DCSS
    // TODO: maybe put this into a derived class for NB data structures?
    padded<pds::sc_desc_t>* local_descs = nullptr;
public:
    // return num of blocks recovered.
    virtual int recover(bool simulated = false) = 0;
    Recoverable(GlobalTestConfig* gtc);
    virtual ~Recoverable();

    void init_thread(GlobalTestConfig*, LocalTestConfig* ltc);
    void init_thread(int tid);
    bool check_epoch(){
        return _esys->check_epoch(epochs[pds::EpochSys::tid].ui);
    }
    bool check_epoch(uint64_t c){
        return _esys->check_epoch(c);
    }
    void begin_op(){
        assert(epochs[pds::EpochSys::tid].ui == NULL_EPOCH);
        epochs[pds::EpochSys::tid].ui = _esys->begin_transaction();
        // TODO: any room for optimization here?
        // TODO: put pending_allocs-related stuff into operations?
        for (auto b = pending_allocs[pds::EpochSys::tid].ui.begin(); 
            b != pending_allocs[pds::EpochSys::tid].ui.end(); b++){
            assert((*b)->get_epoch() == NULL_EPOCH);
            _esys->register_alloc_pblk(*b, epochs[pds::EpochSys::tid].ui);
        }
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
    }
    void end_op(){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
            _esys->end_transaction(epochs[pds::EpochSys::tid].ui);
            last_epochs[pds::EpochSys::tid].ui = epochs[pds::EpochSys::tid].ui;
            epochs[pds::EpochSys::tid].ui = NULL_EPOCH;
        }
        if(!pending_allocs[pds::EpochSys::tid].ui.empty()) 
            pending_allocs[pds::EpochSys::tid].ui.clear();
    }
    void end_readonly_op(){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
            _esys->end_readonly_transaction(epochs[pds::EpochSys::tid].ui);
            epochs[pds::EpochSys::tid].ui = NULL_EPOCH;
        }
        assert(pending_allocs[pds::EpochSys::tid].ui.empty());
    }
    void abort_op(){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        // TODO: any room for optimization here?
        for (auto b = pending_allocs[pds::EpochSys::tid].ui.begin(); 
            b != pending_allocs[pds::EpochSys::tid].ui.end(); b++){
            // reset epochs registered in pending blocks
            _esys->reset_alloc_pblk(*b);
        }
        _esys->abort_transaction(epochs[pds::EpochSys::tid].ui);
        epochs[pds::EpochSys::tid].ui = NULL_EPOCH;
    }
    class MontageOpHolder{
        Recoverable* ds = nullptr;
    public:
        MontageOpHolder(Recoverable* ds_): ds(ds_){
            ds->begin_op();
        }
        ~MontageOpHolder(){
            ds->end_op();
        }
    };
    class MontageOpHolderReadOnly{
        Recoverable* ds = nullptr;
    public:
        MontageOpHolderReadOnly(Recoverable* ds_): ds(ds_){
            ds->begin_op();
        }
        ~MontageOpHolderReadOnly(){
            ds->end_readonly_op();
        }
    };
    pds::PBlk* pmalloc(size_t sz) 
    {
        pds::PBlk* ret = (pds::PBlk*)_esys->malloc_pblk(sz);
        if (epochs[pds::EpochSys::tid].ui == NULL_EPOCH){
            pending_allocs[pds::EpochSys::tid].ui.push_back(ret);
        } else {
            _esys->register_alloc_pblk(ret, epochs[pds::EpochSys::tid].ui);
        }
        return (pds::PBlk*)ret;
    }
    template <typename T, typename... Types> 
    T* pnew(Types... args) 
    {
        T* ret = _esys->new_pblk<T>(args...);
        if (epochs[pds::EpochSys::tid].ui == NULL_EPOCH){
            pending_allocs[pds::EpochSys::tid].ui.push_back(ret);
        } else {
            _esys->register_alloc_pblk(ret, epochs[pds::EpochSys::tid].ui);
        }
        return ret;
    }
    template<typename T>
    void register_update_pblk(T* b){
        _esys->register_update_pblk(b, epochs[pds::EpochSys::tid].ui);
    }
    template<typename T>
    void pdelete(T* b){
        ASSERT_DERIVE(T, pds::PBlk);
        ASSERT_COPY(T);

        if (_esys->sys_mode == pds::ONLINE){
            if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
                _esys->free_pblk(b, epochs[pds::EpochSys::tid].ui);
            } else {
                if (((pds::PBlk*)b)->get_epoch() == NULL_EPOCH){
                    std::reverse_iterator pos = std::find(pending_allocs[pds::EpochSys::tid].ui.rbegin(),
                        pending_allocs[pds::EpochSys::tid].ui.rend(), b);
                    assert(pos != pending_allocs[pds::EpochSys::tid].ui.rend());
                    pending_allocs[pds::EpochSys::tid].ui.erase((pos+1).base());
                }
                _esys->delete_pblk(b);
            }
        }
    }
    template<typename T>
    void pretire(T* b){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        _esys->retire_pblk(b, epochs[pds::EpochSys::tid].ui);
    }
    template<typename T>
    void preclaim(T* b){
        bool not_in_operation = false;
        if (epochs[pds::EpochSys::tid].ui == NULL_EPOCH){
            not_in_operation = true;
            begin_op();
        }
        _esys->reclaim_pblk(b, epochs[pds::EpochSys::tid].ui);
        if (not_in_operation){
            end_op();
        }
    }
    template<typename T>
    const T* openread_pblk(const T* b){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        return _esys->openread_pblk(b, epochs[pds::EpochSys::tid].ui);
    }
    template<typename T>
    const T* openread_pblk_unsafe(const T* b){
        // Wentao: skip checking epoch here since this may be called
        // during recovery, which may not have epochs[tid]
        // if (epochs[pds::EpochSys::tid].ui != NULL_EPOCH){
        //     return _esys->openread_pblk_unsafe(b, epochs[pds::EpochSys::tid].ui);
        // } else {
            return b;
        // }
    }
    template<typename T>
    T* openwrite_pblk(T* b){
        assert(epochs[pds::EpochSys::tid].ui != NULL_EPOCH);
        return _esys->openwrite_pblk(b, epochs[pds::EpochSys::tid].ui);
    }
    std::unordered_map<uint64_t, pds::PBlk*>* recover_pblks(const int rec_thd=10){
        return _esys->recover(rec_thd);
    }
    void sync(){
        assert(epochs[pds::EpochSys::tid].ui == NULL_EPOCH);
        _esys->sync(last_epochs[pds::EpochSys::tid].ui);
    }
    void recover_mode(){
        _esys->sys_mode = pds::RECOVER; // PDELETE -> nop
    }
    void online_mode(){
        _esys->sys_mode = pds::ONLINE;
    }
    void flush(){
        _esys->flush();
    }
    void simulate_crash(){
        _esys->simulate_crash();
    }

    pds::sc_desc_t* get_dcss_desc(){
        return &local_descs[pds::EpochSys::tid].ui;
    }
    uint64_t get_local_epoch(){
        return epochs[pds::EpochSys::tid].ui;
    }
};

/////////////////////////////
// field generation macros //
/////////////////////////////

// macro for concatenating two tokens into a new token
#define TOKEN_CONCAT(a,b)  a ## b

/**
 *  using the type t and the name n, generate a protected declaration for the
 *  field, as well as public getters and setters
 */
#define GENERATE_FIELD(t, n, T)\
/* declare the field, with its name prefixed by m_ */\
protected:\
    t TOKEN_CONCAT(m_, n);\
public:\
/* get method open a pblk for read. */\
t TOKEN_CONCAT(get_, n)(Recoverable* ds) const{\
    return ds->openread_pblk(this)->TOKEN_CONCAT(m_, n);\
}\
/* get method open a pblk for read. Allows old-see-new reads. */\
t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds) const{\
    return ds->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n);\
}\
/* set method open a pblk for write. return a new copy when necessary */\
template <class in_type>\
T* TOKEN_CONCAT(set_, n)(Recoverable* ds, const in_type& TOKEN_CONCAT(tmp_, n)){\
    assert(ds->get_local_epoch() != NULL_EPOCH);\
    auto ret = ds->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
    ds->register_update_pblk(ret);\
    return ret;\
}\
/* set the field by the parameter. called only outside BEGIN_OP and END_OP */\
template <class in_type>\
void TOKEN_CONCAT(set_unsafe_, n)(Recoverable* ds, const in_type& TOKEN_CONCAT(tmp_, n)){\
    TOKEN_CONCAT(m_, n) = TOKEN_CONCAT(tmp_, n);\
}

/**
 *  using the type t, the name n and length s, generate a protected
 *  declaration for the field, as well as public getters and setters
 */
#define GENERATE_ARRAY(t, n, s, T)\
/* declare the field, with its name prefixed by m_ */\
protected:\
    t TOKEN_CONCAT(m_, n)[s];\
/* get method open a pblk for read. */\
t TOKEN_CONCAT(get_, n)(Recoverable* ds, int i) const{\
    return ds->openread_pblk(this)->TOKEN_CONCAT(m_, n)[i];\
}\
/* get method open a pblk for read. Allows old-see-new reads. */\
t TOKEN_CONCAT(get_unsafe_, n)(Recoverable* ds, int i) const{\
    return ds->openread_pblk_unsafe(this)->TOKEN_CONCAT(m_, n)[i];\
}\
/* set method open a pblk for write. return a new copy when necessary */\
T* TOKEN_CONCAT(set_, n)(Recoverable* ds, int i, t TOKEN_CONCAT(tmp_, n)){\
    assert(ds->get_local_epoch() != NULL_EPOCH);\
    auto ret = ds->openwrite_pblk(this);\
    ret->TOKEN_CONCAT(m_, n)[i] = TOKEN_CONCAT(tmp_, n);\
    ds->register_update_pblk(ret);\
    return ret;\
}

namespace pds{

    template<typename T>
    void atomic_lin_var<T>::store(const T& desired){
        // this function must be used only when there's no data race
        lin_var r = var.load();
        lin_var new_r(reinterpret_cast<uint64_t>(desired),r.cnt);
        var.store(new_r);
    }

#ifdef VISIBLE_READ
    // implementation of load and cas for visible reads

    template<typename T>
    lin_var atomic_lin_var<T>::load(Recoverable* ds){
        lin_var r;
        while(true){
            r = var.load();
            lin_var ret(r.val,r.cnt+1);
            if(var.compare_exchange_strong(r, ret))
                return ret;
        }
    }

    template<typename T>
    lin_var atomic_lin_var<T>::load_verify(Recoverable* ds){
        assert(ds->get_local_epoch() != NULL_EPOCH);
        lin_var r;
        while(true){
            r = var.load();
            if(ds->_esys->check_epoch()){
                lin_var ret(r.val,r.cnt+1);
                if(var.compare_exchange_strong(r, ret)){
                    return r;
                }
            } else {
                throw EpochVerifyException();
            }
        }
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS_verify(Recoverable* ds, lin_var expected, const T& desired){
        assert(ds->get_local_epoch() != NULL_EPOCH);
        if(ds->_esys->check_epoch()){
            lin_var new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
            return var.compare_exchange_strong(expected, new_r);
        } else {
            return false;
        }
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS(lin_var expected, const T& desired){
        lin_var new_r(reinterpret_cast<uint64_t>(desired),expected.cnt+1);
        return var.compare_exchange_strong(expected, new_r);
    }

#else /* !VISIBLE_READ */
    /* implementation of load and cas for invisible reads */

    template<typename T>
    lin_var atomic_lin_var<T>::load(Recoverable* ds){
        lin_var r;
        do { 
            r = var.load();
            if(r.is_desc()) {
                sc_desc_t* D = r.get_desc();
                D->try_complete(ds, reinterpret_cast<uint64_t>(this));
            }
        } while(r.is_desc());
        return r;
    }

    template<typename T>
    lin_var atomic_lin_var<T>::load_verify(Recoverable* ds){
        // invisible read doesn't need to verify epoch even if it's a
        // linearization point
        // this saves users from catching EpochVerifyException
        return load(ds);
    }

    // extern std::atomic<size_t> abort_cnt;
    // extern std::atomic<size_t> total_cnt;

    template<typename T>
    bool atomic_lin_var<T>::CAS_verify(Recoverable* ds, lin_var expected, const T& desired){
        assert(ds->get_local_epoch() != NULL_EPOCH);
        // total_cnt.fetch_add(1);
#ifdef USE_TSX
        unsigned status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            lin_var r = var.load();
            if(!r.is_desc()){
                if( r.cnt!=expected.cnt ||
                    r.val!=expected.val ||
                    !ds->check_epoch()){
                    _xend();
                    return false;
                } else {
                    lin_var new_r (reinterpret_cast<uint64_t>(desired), r.cnt+4);
                    var.store(new_r);
                    _xend();
                    return true;
                }
            } else {
                // we only help complete descriptor, but not retry
                _xend();
                r.get_desc()->try_complete(ds, reinterpret_cast<uint64_t>(this));
                return false;
            }
            // execution won't reach here; program should have returned
            assert(0);
        }
#endif
        // txn fails; fall back routine
        // abort_cnt.fetch_add(1);
        lin_var r = var.load();
        if(r.is_desc()){
            sc_desc_t* D = r.get_desc();
            D->try_complete(ds, reinterpret_cast<uint64_t>(this));
            return false;
        } else {
            if( r.cnt!=expected.cnt || 
                r.val!=expected.val) {
                return false;
            }
        }
        // now r.cnt must be ..00, and r.cnt+1 is ..01, which means "var
        // contains a descriptor" and "a descriptor is in progress"
        assert((r.cnt & 3UL) == 0UL);
        new (ds->get_dcss_desc()) sc_desc_t(r.cnt+1, 
                                    reinterpret_cast<uint64_t>(this), 
                                    expected.val, 
                                    reinterpret_cast<uint64_t>(desired), 
                                    ds->get_local_epoch());
        lin_var new_r(reinterpret_cast<uint64_t>(ds->get_dcss_desc()), r.cnt+1);
        if(!var.compare_exchange_strong(r,new_r)){
            return false;
        }
        ds->get_dcss_desc()->try_complete(ds, reinterpret_cast<uint64_t>(this));
        if(ds->get_dcss_desc()->committed()) return true;
        else return false;
    }

    template<typename T>
    bool atomic_lin_var<T>::CAS(lin_var expected, const T& desired){
        // CAS doesn't check epoch; just cas ptr to desired, with cnt+=4
        assert(!expected.is_desc());
        lin_var new_r(reinterpret_cast<uint64_t>(desired), expected.cnt + 4);
        if(!var.compare_exchange_strong(expected,new_r)){
            return false;
        }
        return true;
    }

#endif /* !VISIBLE_READ */
} // namespace pds

#endif