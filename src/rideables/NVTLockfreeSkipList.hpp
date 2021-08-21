#ifndef NVT_LOCKFREESKIPLIST_H
#define NVT_LOCKFREESKIPLIST_H
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
class NVTLockfreeSkipList : public RMap<K, V> {
public:
    class Payload : public Persistent {
    public:
        V val;
        Payload(): Payload({}){ }
        Payload(V y): val(y){
            clwb_obj_nofence(this);
        }
    }__attribute__((aligned(CACHELINE_SIZE)));
private:
    const static int MAX_LEVELS = 20;
    inline int idx(int a, int b){
        return (a + b) % MAX_LEVELS;
    }

    struct Node;
    struct NodePtr {
        std::atomic<Node *> ptr;
        NodePtr(Node *n) : ptr(n){};
        NodePtr() : ptr(nullptr){};
    };
    struct PayloadPtr {
        std::atomic<Payload *> ptr;
        PayloadPtr(Payload *p) : ptr(p){};
        PayloadPtr() : ptr(nullptr){};
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

    struct alignas(64) Node : public Persistent {
        K key;
        PayloadPtr payload;
        NodePtr prev;
        NodePtr next;
        unsigned long level;
        NodePtr succs[MAX_LEVELS];
        bool marker;
        std::atomic<bool> raise_or_remove;

        Node(K _key, Payload *_payload, Node *_prev, Node *_next, unsigned long _level, bool _marker=false) :
                    key(_key), payload(_payload), prev(_prev),
                    next(_next), level(_level), marker(_marker), raise_or_remove(false) {
            clwb_obj_nofence(this);
            for(int i = 0; i < MAX_LEVELS; i++)
                succs[i].ptr.store((Node *) nullptr);
        };

        Node(Payload *_payload, Node *_prev, Node *_next, unsigned long _level) :
                    Node({},_payload,_prev,_next,_level,false) {};

        Node(Node *_prev, Node *_next) :
                    Node({}, (Payload*)this, _prev, _next, 0, true) {};
    };

    Node *alloc_marker_node(Node *_prev, Node *_next){
        Node *n = new Node(_prev, _next);
        // n->payload.ptr.store((Payload *) n);
        return n;
    }
    void dealloc_marker_node(Node *n){
        delete n;
    }

    Node *initialHeadNode;
    NodePtr head;
    std::atomic<unsigned long> sl_zero{0UL};

    std::atomic<background_state> bg_state{INITIAL};

    int bg_non_deleted = 0;
    int bg_deleted = 0;
    int bg_tall_deleted = 0;
    int bg_sleep_time = 50;

    int bg_should_delete = 1;
    std::thread background_thread;

    RCUTracker tracker;

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
    int internal_finish_delete(const K& key, Node *node, Payload *node_payload, optional<V>& ret_value, int tid);
    int internal_finish_insert(const K& key, V &val, Node *node, Payload *node_payload, Node *next, Payload *&lazy_payload);
    bool internal_do_operation(operation_type optype, const K& key, optional<V>& val, optional<V>& ret_value, int tid);
public:
    NVTLockfreeSkipList(int task_num) : tracker(task_num + 1, 100, 1000, true) {
        Persistent::init();
        int bg_tid = task_num;
        initialHeadNode = new Node(nullptr, nullptr, nullptr, 1);
        head.ptr.store(initialHeadNode);
        background_thread = std::move(std::thread(&NVTLockfreeSkipList::bg_loop, this, bg_tid));
    };
    ~NVTLockfreeSkipList(){
        bg_state.store(background_state::FINISHED);
        background_thread.join();
        Persistent::finalize();
    };
    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Persistent::init_thread(gtc, ltc);
    }

    optional<V> get(K key, int tid);
    optional<V> remove(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> replace(K key, V val, int tid);
};

template<class T>
class NVTLockfreeSkipListFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new NVTLockfreeSkipList<T,T>(gtc->task_num);
    }
};

template<class K, class V>
void NVTLockfreeSkipList<K,V>::bg_loop(int tid){
    Persistent::init_thread(tid);
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
        if (bg_tall_deleted > threshold and local_head->level > 1) {
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
int NVTLockfreeSkipList<K,V>::bg_trav_nodes(int tid){
    Node *prev, *node, *next;
    Node *above_head = head.ptr.load(), *above_prev, *above_next;
    unsigned long zero = sl_zero.load();
    int raised = 0;

    assert(nullptr != head.ptr.load());

    tracker.start_op(tid);

    above_prev = above_next = above_head;
    prev = head.ptr.load();
    node = prev->next.ptr.load();
    // clwb_obj_nofence(prev);
    if (nullptr == node)
        return 0;
    next = node->next.ptr.load();
    // clwb_obj_nofence(node);

    while (nullptr != next) {

        bool expected = false;
        Payload *payload_ptr = node->payload.ptr.load();
        // clwb_obj_nofence(node);
        if (nullptr == payload_ptr) {
            bg_remove(prev, node, tid);
            if (node->level >= 1)
                ++bg_tall_deleted;
            ++bg_deleted;
        } else if ((void *) payload_ptr != (void *) node) {
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

        payload_ptr = node->payload.ptr.load();
        // clwb_obj_nofence(node);
        if (nullptr != payload_ptr && (void *) node != (void *) payload_ptr) {
            ++bg_non_deleted;
        }
        prev = node;
        node = next;
        next = next->next.ptr.load();
        // if(nullptr != next){
        //     clwb_obj_nofence(next);
        // }
    }

    tracker.end_op(tid);

    return raised;
}


template<class K, class V>
void NVTLockfreeSkipList<K,V>::get_index_above(Node *above_head,
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
int NVTLockfreeSkipList<K,V>::bg_raise_ilevel(int h, int tid){
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
        Payload *index_payload = index->payload.ptr.load();
        clwb_obj_fence(index);
        while ((void *) index_payload == (void *) index) {

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
        index_payload = index->payload.ptr.load();
        clwb_obj_fence(index);
        if ( ((((int) iprev->level <= h) &&
                ((int) index->level == h)) &&
                ((int) inext->level <= h)) &&
                ((void *) index_payload != (void *) index &&
                nullptr != index_payload) ) {
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
void NVTLockfreeSkipList<K,V>::bg_lower_ilevel(int tid){
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
void NVTLockfreeSkipList<K,V>::bg_help_remove(Node *prev, Node *node, int tid){
    // Required before calling this function: tracker.start_op(tid) has been called,
    // and tracker.end_op(tid) hasn't been called.
    Node *n, *new_node, *prev_next, *n_next;
    int retval;

    assert(nullptr != prev);
    assert(nullptr != node);

    Payload *payload_ptr = node->payload.ptr.load();
    // clwb_obj_nofence(node);
    if ((void *) payload_ptr != (void *) node || node->marker)
        return;

    n = node->next.ptr.load();
    clwb_obj_nofence(node);
    while (nullptr == n || !n->marker) {
        new_node = alloc_marker_node(node, n);
        sfence();
        if(!node->next.ptr.compare_exchange_strong(n, new_node)){
            // clwb_obj_fence(node);
            dealloc_marker_node(new_node);
        }
        // else {
            // clwb_obj_fence(node);
        // }

        n = node->next.ptr.load();
        clwb_obj_nofence(node);
    }

    prev_next = prev->next.ptr.load();
    // clwb_obj_nofence(prev);
    if (prev_next != node || prev->marker)
        return;

    /* remove the nodes */
    n_next = n->next.ptr.load();
    clwb_obj_fence(n);
    // sfence();
    retval = prev->next.ptr.compare_exchange_strong(node, n_next);
    clwb_obj_fence(prev);

    if (retval) {
        tracker.retire(node, tid);
        tracker.retire(n, tid);
    }

    /*
     * update the prev pointer - we don't need synchronisation here
     * since the prev pointer does not need to be exact
     */
    prev_next = prev->next.ptr.load();
    // clwb_obj_fence(prev);
    if (nullptr != prev_next)
        prev_next->prev.ptr.store(prev);
}

template<class K, class V>
void NVTLockfreeSkipList<K,V>::bg_remove(Node *prev, Node *node, int tid){
    assert(nullptr != node);

    if (0 == node->level) {
        /* only remove short nodes */

        /* CAS expects a reference as the first argument, so we create a temporary variable as a shim. */
        Payload *tmp = nullptr;
        // sfence();
        node->payload.ptr.compare_exchange_strong(tmp, (Payload *) node);
        // clwb_obj_fence(node);
        Payload *payload_ptr = node->payload.ptr.load();
        // clwb_obj_nofence(node);
        if ((void *) payload_ptr == (void *) node)
            bg_help_remove(prev, node, tid);
    }
}

template<class K, class V>
int NVTLockfreeSkipList<K,V>::internal_finish_contains(const K& key, Node *node, Payload *node_payload, optional<V>& ret_value){
    int result = 0;

    assert(nullptr != node);

    if ((key == node->key) && (nullptr != node_payload)) {
        ret_value = V(node_payload->val);
        result = 1;
    }

    return result;
}

template<class K, class V>
int NVTLockfreeSkipList<K,V>::internal_finish_delete(const K& key, Node *node, Payload *node_payload, optional<V>& ret_value, int tid){
    int result = -1;

    assert(nullptr != node);

    if (node->key != key)
        result = 0;
    else {
        if (nullptr != node_payload) {
            /* loop until we or someone else deletes */
            while (1) {
                node_payload = node->payload.ptr.load();
                if (nullptr == node_payload || (void *) node == (void *) node_payload) {
                    result = 0;
                    clwb_obj_fence(node);
                    break;
                }
                else {
                    sfence();
                    if (node->payload.ptr.compare_exchange_strong(node_payload, (Payload *) nullptr)) {
                        clwb_obj_fence(node);
                        ret_value = V(node_payload->val);
                        tracker.retire(node_payload, tid);

                        result = 1;
                        if (bg_should_delete) {
                            bool expected = false;
                            if (node->raise_or_remove.compare_exchange_strong(expected, true)) {
                                bg_remove(node->prev.ptr.load(), node, tid);
                            }
                        }
                        break;
                    } 
                    // else {
                    //     clwb_obj_fence(node);
                    // }
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
int NVTLockfreeSkipList<K,V>::internal_finish_insert(const K& key, V &val, Node *node, Payload *node_payload, Node *next, Payload *&lazy_payload){
    int result = -1;
    Node *new_node, *temp;

    if(lazy_payload == nullptr){
        lazy_payload = new Payload(key, val);
    }
    if (node->key == key) {
        if (nullptr == node_payload) {
            sfence();
            if (node->payload.ptr.compare_exchange_strong(node_payload, lazy_payload)){
                clwb_obj_fence(node);
                result = 1;
            } else {
                clwb_obj_fence(node);
            }
        } else {
            result = 0;
        }
    } else {
        new_node = new Node(key, lazy_payload, node, next, 0);
        sfence();
        if (node->next.ptr.compare_exchange_strong(next, new_node)) {
            clwb_obj_fence(node);
            if (nullptr != next) {
                temp = next->prev.ptr.load();
                next->prev.ptr.compare_exchange_strong(temp, new_node);
            }
            result = 1;
        } else {
            clwb_obj_fence(node);
            delete new_node;
        }
    }

    return result;
}

template<class K, class V>
bool NVTLockfreeSkipList<K,V>::internal_do_operation(operation_type optype, const K& key, optional<V>& val, optional<V>& ret_value, int tid){
    Node *item = nullptr, *next_item = nullptr;
    Node *node = nullptr, *next = nullptr;
    Node *local_head;
    Payload *node_payload, *next_payload = nullptr;
    int result = 0;
    unsigned long zero, i;

    tracker.start_op(tid);

    zero = sl_zero.load();
    local_head = head.ptr.load();
    i = local_head->level - 1;

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

    // lazily created, to avoid accessing NVM repeatedly.
    Payload *insert_payload=nullptr;

    /* find the correct node and next */
    while (1) {
        node_payload = node->payload.ptr.load();
        while ((void *) node == (void *) node_payload) {
            node = node->prev.ptr.load();
            node_payload = node->payload.ptr.load();
            // clwb_obj_nofence(node);
        }
        next = node->next.ptr.load();
        clwb_obj_nofence(node);
        if (nullptr != next) {
            next_payload = next->payload.ptr.load();
            clwb_obj_nofence(next);
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

    if(optype == INSERT && insert_payload != nullptr and result == false){
        delete insert_payload;
    }
    tracker.end_op(tid);
    sfence();
    return result;
}

template<class K, class V>
bool NVTLockfreeSkipList<K,V>::insert(K key, V val, int tid)
{
    optional<V> unused = {};
    optional<V> val_opt = val;
    bool ret = internal_do_operation(operation_type::INSERT, key, val_opt, unused, tid);
    // sfence();
    return ret;
}

template<class K, class V>
optional<V> NVTLockfreeSkipList<K,V>::get(K key, int tid)
{
    optional<V> res = {};
    optional<V> unused = {};
    internal_do_operation(operation_type::CONTAINS, key, unused, res, tid);
    // sfence();
    return res;
}

template<class K, class V>
optional<V> NVTLockfreeSkipList<K,V>::remove(K key, int tid)
{
    optional<V> res;
    optional<V> unused = {};
    internal_do_operation(operation_type::DELETE, key, unused, res, tid);
    // sfence();
    return res;
}

template<class K, class V>
optional<V> NVTLockfreeSkipList<K,V>::put(K key, V val, int tid)
{
    optional<V> res;

    assert(0&&"put not implemented!");
    // sfence();
    return res;
}

template<class K, class V>
optional<V> NVTLockfreeSkipList<K,V>::replace(K key, V val, int tid)
{
    optional<V> res;

    assert(0&&"replace not implemented!");
    sfence();
    return res;
}

/* Specialization for strings */
#include <string>
#include "InPlaceString.hpp"
template <>
class NVTLockfreeSkipList<std::string, std::string>::Payload : public Persistent {
public:
    pds::InPlaceString<TESTS_KEY_SIZE> key;
    pds::InPlaceString<TESTS_VAL_SIZE> val;
    Payload(){};
    Payload(std::string k, std::string v) : key(k), val(v){};
};

#endif
