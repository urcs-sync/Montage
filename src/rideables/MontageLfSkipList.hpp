#ifndef MONTAGELFSKIPLIST_H
#define MONTAGELFSKIPLIST_H
#include <cstdio>
#include <cstdlib>

#include <iostream>
#include <atomic>
#include <algorithm>
#include <functional>
#include <vector>
#include <utility>

#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RMap.hpp"
#include "RCUTracker.hpp"

template<class K, class V>
class MontageLfSkipList : public RMap<K, V>, public Recoverable {
public:
    class Payload : public pds::PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y){}
        Payload(const Payload& oth): pds::PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    }__attribute__((aligned(CACHELINE_SIZE)));
private:
    const static int MAX_LEVELS = 20;
    inline int idx(int a, int b){
        return (a + b) % MAX_LEVELS;
    }

    struct Node;
    struct TransientNodePtr {
        std::atomic<Node *> ptr;
        TransientNodePtr(Node *n) : ptr(n){};
        TransientNodePtr() : ptr(nullptr){};
    };
    struct PdsNodePtr {
        pds::atomic_lin_var<Node *> ptr;
        PdsNodePtr(Node *n) : ptr(n){};
        PdsNodePtr() : ptr(nullptr){};
    };
    enum operation_type {
        CONTAINS,
        DELETE,
        INSERT
    };

    enum background_state {
        INITIAL,
        RUNNING,
        FINISHED
    };

    struct alignas(64) Node {
        K key;
        pds::atomic_lin_var<Payload *> payload;
        TransientNodePtr prev;
        PdsNodePtr next;
        unsigned long level;
        TransientNodePtr succs[MAX_LEVELS];
        bool marker;
        std::atomic<bool> raise_or_remove;

        Node(K _key, Payload *_payload, Node *_prev, Node *_next, unsigned long _level) :
                    key(_key), payload(_payload), prev(_prev),
                    next(_next), level(_level), marker(false), raise_or_remove(false) {
            for(int i = 0; i < MAX_LEVELS; i++)
                succs[i].ptr.store((Node *) nullptr);
        };

        Node(Payload *_payload, Node *_prev, Node *_next, unsigned long _level) :
                    payload(_payload), prev(_prev), next(_next),
                    level(_level), marker(false), raise_or_remove(false) {
            for(int i = 0; i < MAX_LEVELS; i++)
                succs[i].ptr.store((Node *) nullptr);
        };

        Node(Node *_prev, Node *_next) :
                    prev(_prev), next(_next), level(0), marker(true), raise_or_remove(false) {
            for(int i = 0; i < MAX_LEVELS; i++)
                succs[i].ptr.store((Node *) nullptr);
        }
    };

    Node *alloc_marker_node(Node *_prev, Node *_next){
        Node *n = new Node(_prev, _next);
        n->payload.store(this, (Payload *) n);
        return n;
    }
    void dealloc_marker_node(Node *n){
        assert((void *) n->payload.load(this) == (void *) n);
        delete n;
    }

    Node *initialHeadNode = new Node(nullptr, nullptr, nullptr, 1);
    TransientNodePtr head = TransientNodePtr(initialHeadNode);
    std::atomic<unsigned long> sl_zero = std::atomic<unsigned long>(0UL);
    std::atomic<bool> should_cas_verify = std::atomic<bool>(true);
    std::atomic<background_state> bg_state = std::atomic<background_state>(INITIAL);

    int bg_non_deleted = 0;
    int bg_deleted = 0;
    int bg_tall_deleted = 0;
    int bg_sleep_time = 50;

    int bg_should_delete = 1;
    std::thread background_thread;

    RCUTracker tracker;
    GlobalTestConfig* gtc;

    const uint64_t MARK_MASK = ~0x1;
    inline Node *getPtr(Node *mptr) {
        return (Node *)((uint64_t)mptr & MARK_MASK);
    }
    inline bool getMark(Node *mptr) {
        return (bool)((uint64_t)mptr & 1);
    }
    inline Node *mixPtrMark(Node *ptr, bool mk) {
        return (Node *)((uint64_t)ptr | mk);
    }
    inline Node *setMark(Node *mptr) {
        return mixPtrMark(mptr, true);
    }

    void bg_loop(int tid);
    int bg_trav_nodes(int tid);
    void get_index_above(Node *above_head,
                         Node *&above_prev,
                         Node *&above_next,
                         unsigned long i,
                         const K& key,
                         unsigned long zero);
    int bg_raise_ilevel(int h, int tid);
    void bg_lower_ilevel(int tid);
    void bg_help_remove(Node *prev, Node *node, int tid);
    void bg_remove(Node *prev, Node *node, int tid);
    int internal_finish_contains(const K& key, Node *node, Payload *node_payload, optional<V>& ret_value);
    int internal_finish_delete(const K& key, Node *node, Payload* node_payload, optional<V>& ret_value, int tid);
    int internal_finish_insert(const K& key, V &val, Node *node, Payload* node_payload, Node* next, Payload*& lazy_payload);
    bool internal_do_operation(operation_type optype, const K& key, optional<V>& val, optional<V>& ret_value, int tid, Payload *suggest_payload = nullptr);
public:
    MontageLfSkipList(GlobalTestConfig* gtc) : Recoverable(gtc), tracker(gtc->task_num + 1, 100, 1000, true), gtc(gtc) {
        int bg_tid = gtc->task_num;
        bg_state.store(background_state::RUNNING);
        background_thread = std::move(std::thread(&MontageLfSkipList::bg_loop, this, bg_tid));
    };
    ~MontageLfSkipList(){
        bg_state.store(background_state::FINISHED);
        background_thread.join();
    };

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Recoverable::init_thread(gtc, ltc);
    }
    void clear(){
        //single-threaded; for recovery test only
        unsigned long zero = sl_zero.load();
        Node *node = head.ptr.load()->next.ptr.load(this);
        Node *node_next = node;

        while (node) {
            node_next = node->next.ptr.load(this);
            Payload* node_payload = node->payload.load(this);
            Payload *payload_p = (Payload*)node_payload;
            if(payload_p != nullptr && (void *) payload_p != (void *) node){
                this->preclaim(payload_p);
            }
            delete node;
            node = node_next;
        }

        delete initialHeadNode;
        initialHeadNode = new Node(nullptr, nullptr, nullptr, 1);
        head.ptr.store(initialHeadNode);
    }

    int recover(bool simulated){
        if (simulated){
            recover_mode(); // PDELETE --> noop
            // clear transient structures.
            clear();
            online_mode(); // re-enable PDELETE.
        }

        should_cas_verify.store(false);

        int rec_cnt = 0;
        int rec_thd_count = gtc->task_num;
        if (gtc->checkEnv("RecoverThread")){
            rec_thd_count = stoi(gtc->getEnv("RecoverThread"));
        }
        auto begin = chrono::high_resolution_clock::now();
        std::unordered_map<uint64_t, pds::PBlk*>* recovered = recover_pblks(rec_thd_count);
        auto end = chrono::high_resolution_clock::now();
        auto dur = end - begin;
        auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms << "ms getting PBlk(" << recovered->size() << ")" << std::endl;

        std::vector<Payload*> payloadVector;
        payloadVector.reserve(recovered->size());
        begin = chrono::high_resolution_clock::now();
        for (auto itr = recovered->begin(); itr != recovered->end(); itr++){
            rec_cnt++;
            Payload* payload = reinterpret_cast<Payload*>(itr->second);
            payloadVector.push_back(payload);
        }
        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        auto dur_ms_vec = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_vec << "ms building vector" << std::endl;

        begin = chrono::high_resolution_clock::now();
        std::vector<std::thread> workers;
        for (int rec_tid = 0; rec_tid < rec_thd_count; rec_tid++) {
            workers.emplace_back(std::thread([&, rec_tid]() {
                Recoverable::init_thread(rec_tid);
                hwloc_set_cpubind(gtc->topology,
                                  gtc->affinities[rec_tid]->cpuset,
                                  HWLOC_CPUBIND_THREAD);
                for (size_t i = rec_tid; i < payloadVector.size(); i += rec_thd_count) {
                    // re-insert payload.
                    K key = payloadVector[i]->get_unsafe_key(this);
                    V val = payloadVector[i]->get_unsafe_val(this);
                    while (true) {
                        optional<V> res = {};
                        optional<V> unused = {};
                        if(internal_do_operation(operation_type::CONTAINS, key, unused, res, rec_tid)){
                            errexit("conflicting keys recovered.");
                        } else {
                            optional<V> val_opt = val;
                            if(internal_do_operation(operation_type::INSERT, key, val_opt, unused, rec_tid, payloadVector[i])){
                                break;
                            } else {
                                cout << "Hmm!" << endl;
                            }
                        }
                    }
                }
            }));
        }
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        end = chrono::high_resolution_clock::now();
        dur = end - begin;
        auto dur_ms_ins = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
        std::cout << "Spent " << dur_ms_ins << "ms inserting(" << recovered->size() << ")" << std::endl;
        std::cout << "Total time to recover: " << dur_ms+dur_ms_vec+dur_ms_ins << "ms" << std::endl;

        should_cas_verify.store(true);
        delete recovered;
        return rec_cnt;
    }

    optional<V> get(K key, int tid);
    optional<V> remove(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> replace(K key, V val, int tid);
};

template<class T>
class MontageLfSkipListFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageLfSkipList<T,T>(gtc);
    }
};

template<class K, class V>
void MontageLfSkipList<K,V>::bg_loop(int tid){
    Node *local_head  = head.ptr.load();
    int raised = 0; /* keep track of if we raised index level */
    int threshold;  /* for testing if we should lower index level */
    unsigned long i;
    unsigned long zero;

    bg_should_delete = 1;
    std::atomic_signal_fence(std::memory_order_seq_cst);

    while (1) {
        std::this_thread::sleep_for(std::chrono::microseconds(bg_sleep_time));

        if (bg_state.load() == background_state::FINISHED)
            break;

        zero = sl_zero.load();

        bg_non_deleted = 0;
        bg_deleted = 0;
        bg_tall_deleted = 0;

        // traverse the node level and try deletes/raises
        raised = bg_trav_nodes(tid);

        if (raised && (1 == local_head->level)) {
            // add a new index level

            // nullify BEFORE we increase the level
            local_head->succs[idx(local_head->level, zero)].ptr.store(nullptr);
            ++local_head->level;
        }

        // raise the index level nodes
        for (i = 0; (i+1) < head.ptr.load()->level; i++) {
            assert(i < MAX_LEVELS);
            raised = bg_raise_ilevel(i + 1, tid);

            if ((((i+1) == (local_head->level-1)) && raised)
                    && local_head->level < MAX_LEVELS) {
                // add a new index level

                // nullify BEFORE we increase the level
                local_head->succs[idx(local_head->level,zero)].ptr.store(nullptr);
                ++local_head->level;
            }
        }

        // if needed, remove the lowest index level
        threshold = bg_non_deleted * 10;
        if (bg_tall_deleted > threshold && local_head->level > 1) {
            bg_lower_ilevel(tid);
        }

        if (bg_deleted > bg_non_deleted * 3) {
            bg_should_delete = 1;
        } else {
            bg_should_delete = 0;
        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
}

template<class K, class V>
int MontageLfSkipList<K,V>::bg_trav_nodes(int tid){
    Node *prev, *node, *next;
    Node *above_head = head.ptr.load(), *above_prev, *above_next;
    unsigned long zero = sl_zero.load();
    int raised = 0;

    assert(nullptr != head.ptr.load());

    tracker.start_op(tid);

    above_prev = above_next = above_head;
    prev = head.ptr.load();
    node = prev->next.ptr.load(this);
    if (nullptr == node)
        return 0;
    next = node->next.ptr.load(this);

    while (nullptr != next) {

        bool expected = false;
        if (nullptr == node->payload.load(this)) {
            bg_remove(prev, node, tid);
            if (node->level >= 1)
                ++bg_tall_deleted;
            ++bg_deleted;
        } else if ((void *) node->payload.load(this) != (void *) node) {
            if ((((0 == prev->level
                && 0 == node->level)
                && 0 == next->level))
                && node->raise_or_remove.compare_exchange_strong(expected, true)) {

                node->level = 1;

                raised = 1;

                get_index_above(above_head, above_prev,
                        above_next, 0, node->key,
                        zero);

                // swap the pointers
                node->succs[idx(0,zero)].ptr.store(above_next);

                above_prev->succs[idx(0,zero)].ptr.store(node);
                above_next = above_prev = above_head = node;
            }
        }

        if (nullptr != node->payload.load(this) && (void *) node != (void *) node->payload.load(this)) {
            ++bg_non_deleted;
        }
        prev = node;
        node = next;
        next = next->next.ptr.load(this);
    }

    tracker.end_op(tid);

    return raised;
}


template<class K, class V>
void MontageLfSkipList<K,V>::get_index_above(Node *above_head,
                                       Node *&above_prev,
                                       Node *&above_next,
                                       unsigned long i,
                                       const K& key,
                                       unsigned long zero){
    /* get the correct index node above */
    while (above_next && above_next->key < key) {
        above_next = above_next->succs[idx(i,zero)].ptr.load();
        if (above_next != above_head->succs[idx(i,zero)].ptr.load())
            above_prev = above_prev->succs[idx(i,zero)].ptr.load();
    }
}

template<class K, class V>
int MontageLfSkipList<K,V>::bg_raise_ilevel(int h, int tid){
    int raised = 0;
    unsigned long zero = sl_zero.load();
    Node *index, *inext, *iprev = head.ptr.load();
    Node *above_next, *above_prev, *above_head;

    tracker.start_op(tid);

    above_next = above_prev = above_head = head.ptr.load();

    index = iprev->succs[idx(h-1,zero)].ptr.load();
    if (nullptr == index)
        return raised;

    while (nullptr != (inext = index->succs[idx(h-1,zero)].ptr.load())) {
        while ((void *) index->payload.load(this) == (void *) index) {

            // skip deleted nodes
            iprev->succs[idx(h-1,zero)].ptr.store(inext);
            --index->level;

            if (nullptr == inext)
                break;
            index = inext;
            inext = inext->succs[idx(h-1,zero)].ptr.load();
        }
        if (nullptr == inext)
            break;
        if ( ((((int) iprev->level <= h) &&
                ((int) index->level == h)) &&
                ((int) inext->level <= h)) &&
                ((void *) index->payload.load(this) != (void *) index &&
                nullptr != index->payload.load(this)) ) {
            raised = 1;

            /* find the correct index node above */
            get_index_above(above_head, above_prev, above_next,
                    h, index->key, zero);

            /* fix the pointers and levels */
            index->succs[idx(h,zero)].ptr.store(above_next);
            above_prev->succs[idx(h,zero)].ptr.store(index);
            ++index->level;

            above_next = above_prev = above_head = index;
        }
        iprev = index;
        index = index->succs[idx(h-1,zero)].ptr.load();
    }

    tracker.end_op(tid);

    return raised;
}

template<class K, class V>
void MontageLfSkipList<K,V>::bg_lower_ilevel(int tid){
    unsigned long zero = sl_zero.load();
    Node *node = head.ptr.load();
    Node *node_next = node;

    tracker.start_op(tid);

    if (node->level-2 <= zero)
        return; /* no more room to lower */

    /* decrement the level of all nodes */

    while (node) {
        node_next = node->succs[idx(0,zero)].ptr.load();
        if (!node->marker) {
            if (node->level > 0) {
                if (1 == node->level && node->raise_or_remove.load())
                    node->raise_or_remove.store(false);

                /* null out the ptr for level being removed */
                node->succs[idx(0,zero)].ptr.store(nullptr);
                --node->level;
            }
        }
        node = node_next;
    }

    /* remove the lowest index level */
    ++sl_zero;

    tracker.end_op(tid);
}

template<class K, class V>
void MontageLfSkipList<K,V>::bg_help_remove(Node *prev, Node *node, int tid){
    // Required before calling this function: tracker.start_op(tid) has been called,
    // and tracker.end_op(tid) hasn't been called.
    Node *n, *new_node, *prev_next;
    int retval;

    assert(nullptr != prev);
    assert(nullptr != node);

    if ((void *) node->payload.load(this) != (void *) node || node->marker)
        return;

    n = node->next.ptr.load(this);
    while (nullptr == n || !n->marker) {
        new_node = alloc_marker_node(node, n);
        if(!node->next.ptr.CAS(this, n, new_node)){
            dealloc_marker_node(new_node);
        }

        assert (node->next.ptr.load(this) != node);

        n = node->next.ptr.load(this);
    }

    if (prev->next.ptr.load(this) != node || prev->marker)
        return;

    /* remove the nodes */
    retval = prev->next.ptr.CAS(this, node, n->next.ptr.load(this));
    assert (prev->next.ptr.load(this) != prev);

    if (retval) {
        tracker.retire(node, tid);
        tracker.retire(n, tid);
    }

    /*
     * update the prev pointer - we don't need synchronisation here
     * since the prev pointer does not need to be exact
     */
    prev_next = prev->next.ptr.load(this);
    if (nullptr != prev_next)
        prev_next->prev.ptr.store(prev);
}

template<class K, class V>
void MontageLfSkipList<K,V>::bg_remove(Node *prev, Node *node, int tid){
    assert(nullptr != node);

    if (0 == node->level) {
        /* only remove short nodes */
        node->payload.CAS(this, nullptr, (Payload *) node);
        if ((void *) node->payload.load(this) == (void *) node)
            bg_help_remove(prev, node, tid);
    }
}

template<class K, class V>
int MontageLfSkipList<K,V>::internal_finish_contains(const K& key, Node *node, Payload *node_payload, optional<V>& ret_value){
    int result = 0;

    assert(nullptr != node);

    // TODO linearize "gets" operations as well. Most likely not necessary
    if ((key == node->key) && (nullptr != node_payload)) {
        ret_value = V(node_payload->get_unsafe_val(this)); // TODO why unsafe?
        result = 1;
    }

    return result;
}

template<class K, class V>
int MontageLfSkipList<K,V>::internal_finish_delete(const K& key, Node *node, Payload* node_payload, optional<V>& ret_value, int tid){
    int result = -1;

    Payload *payload_p = node_payload;
    assert(nullptr != node);

    if (node->key != key)
        result = 0;
    else {
        if (nullptr != payload_p) {
            /* loop until we or someone else deletes */
            while (1) {
                node_payload = node->payload.load(this);
                payload_p = node_payload;
                if (nullptr == payload_p || (void *) node == (void *) payload_p) {
                    result = 0;
                    break;
                }
                else {
                    this->pretire(payload_p);
                    if (node->payload.CAS_verify(this, node_payload, (Payload *) nullptr)) { // Linearization point
                        ret_value = V(payload_p->get_unsafe_val(this));
                        tracker.retire(payload_p, tid, [&](void* o){
                            this->preclaim((Payload*)o);
                        });

                        result = 1;
                        if (bg_should_delete) {
                            bool expected = false;
                            if (node->raise_or_remove.compare_exchange_strong(expected, true)) {
                                bg_remove(node->prev.ptr.load(), node, tid);
                            }
                        }
                        break;
                    }
                }
            }
        } else {
            /* Already logically deleted */
            result = 0;
        }
    }

    return result;
}

template<class K, class V>
int MontageLfSkipList<K,V>::internal_finish_insert(const K& key, V &val, Node *node, Payload* node_payload, Node* next, Payload*& lazy_payload){
    int result = -1;
    Node *new_node, *temp;
    bool local_should_cas_verify = should_cas_verify.load();

    if(lazy_payload == nullptr){
        lazy_payload = this->pnew<Payload>(key, val);
    }
    if (node->key == key) {
        if (nullptr == node_payload) {
            if ((local_should_cas_verify && node->payload.CAS_verify(this, node_payload, lazy_payload)) ||
                    (!local_should_cas_verify && node->payload.CAS(this,node_payload, lazy_payload))) // Linearization point
                result = 1;
        } else {
            result = 0;
        }
    } else {
        new_node = new Node(key, lazy_payload, node, next, 0);
        if ((local_should_cas_verify && node->next.ptr.CAS_verify(this, next, new_node)) ||
                (!local_should_cas_verify && node->next.ptr.CAS(this,next, new_node))) { // Linearization point
            if (nullptr != next) {
                temp = next->prev.ptr.load();
                next->prev.ptr.compare_exchange_strong(temp, new_node);
            }
            result = 1;
        } else {
            delete new_node;
        }
    }

    return result;
}

template<class K, class V>
bool MontageLfSkipList<K,V>::internal_do_operation(operation_type optype, const K& key, optional<V>& val, optional<V>& ret_value, int tid, Payload *suggest_payload){
    Node *item = nullptr, *next_item = nullptr;
    Node *node = nullptr;
    Node* next;
    Node *local_head = head.ptr.load();
    Payload* node_payload;
    Payload *next_payload = nullptr; 
    int result = 0;
    unsigned long zero, i;

    tracker.start_op(tid);

    zero = sl_zero.load();
    i = head.ptr.load()->level - 1;

    /* find an entry-point to the node-level */
    item = local_head;
    while (1) {
        next_item = item->succs[idx(i,zero)].ptr.load();

        if (nullptr == next_item || next_item->key > key) {

            next_item = item;
            if (zero == i) {
                node = item;
                break;
            } else {
                --i;
            }
        }
        item = next_item;
    }

    // lazily created, only when necessary to avoid costly PNEW.
    Payload *insert_payload=suggest_payload;

    /* find the correct node and next */
    while (1) {
        node_payload = node->payload.load(this);
        while ((void *) node == (void *) node_payload) {
            node = node->prev.ptr.load();
            node_payload = node->payload.load(this);
        }
        next = node->next.ptr.load(this);
        if (nullptr != next) {
            next_payload = next->payload.load(this);
            if ((void *) next_payload == (void *) next) {
                bg_help_remove(node, next, tid);
                continue;
            }
        }
        if (nullptr == next || next->key > key) {
            if (CONTAINS == optype)
                result = internal_finish_contains(key, node, node_payload, ret_value);
            else if (DELETE == optype)
                result = internal_finish_delete(key, node, node_payload, ret_value, tid);
            else if (INSERT == optype)
                result = internal_finish_insert(key, *val, node, node_payload, next, insert_payload);
            if (-1 != result)
                break;
            continue;
        }
        node = next;
    }

    if(optype == INSERT && insert_payload != nullptr && result == false){
        this->preclaim(insert_payload);
    }
    tracker.end_op(tid);

    return result;
}

template<class K, class V>
bool MontageLfSkipList<K,V>::insert(K key, V val, int tid)
{
    optional<V> unused = {};
    optional<V> val_opt = val;
    return internal_do_operation(operation_type::INSERT, key, val_opt, unused, tid);
}

template<class K, class V>
optional<V> MontageLfSkipList<K,V>::get(K key, int tid)
{
    optional<V> res = {};
    optional<V> unused = {};
    internal_do_operation(operation_type::CONTAINS, key, unused, res, tid);
    return res;
}

template<class K, class V>
optional<V> MontageLfSkipList<K,V>::remove(K key, int tid)
{
    optional<V> res;
    optional<V> unused = {};
    internal_do_operation(operation_type::DELETE, key, unused, res, tid);
    return res;
}

template<class K, class V>
optional<V> MontageLfSkipList<K,V>::put(K key, V val, int tid)
{
    optional<V> res;

    assert(0&&"put not implemented!");

    return res;
}

template<class K, class V>
optional<V> MontageLfSkipList<K,V>::replace(K key, V val, int tid)
{
    optional<V> res;

    assert(0&&"replace not implemented!");

    return res;
}

/* Specialization for strings */
#include <string>
#include "PString.hpp"
template <>
class MontageLfSkipList<std::string, std::string>::Payload : public pds::PBlk{
    GENERATE_FIELD(pds::PString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(pds::PString<TESTS_VAL_SIZE>, val, Payload);

public:
    Payload(std::string k, std::string v) : m_key(this, k), m_val(this, v){}
    Payload(const Payload& oth) : pds::PBlk(oth), m_key(this, oth.m_key), m_val(this, oth.m_val){}
    void persist(){}
};

#endif
