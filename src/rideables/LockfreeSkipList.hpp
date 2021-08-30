#ifndef LOCKFREESKIPLIST_HPP
#define LOCKFREESKIPLIST_HPP
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
class LockfreeSkipList : public RMap<K, V> {
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
    struct VPtr {
        std::atomic<V *> ptr;
        VPtr(V *n) : ptr(n){};
        VPtr() : ptr(nullptr){};
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
        VPtr val;
        NodePtr prev;
        NodePtr next;
        unsigned long level;
        NodePtr succs[MAX_LEVELS];
        bool marker;
        std::atomic<bool> raise_or_remove;

        Node(K _key, V *_val, Node *_prev, Node *_next, unsigned long _level) :
                    key(_key), val(_val), prev(_prev),
                    next(_next), level(_level), marker(false), raise_or_remove(false) {
            for(int i = 0; i < MAX_LEVELS; i++)
                succs[i].ptr.store((Node *) nullptr);
        };

        Node(V *_val, Node *_prev, Node *_next, unsigned long _level) :
                    val(_val), prev(_prev), next(_next),
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
        n->val.ptr.store((V *) n);
        return n;
    }
    void dealloc_marker_node(Node *n){
        assert((void *) n->val.ptr.load() == (void *) n);
        delete n;
    }

    Node *initialHeadNode = new Node(nullptr, nullptr, nullptr, 1);
    NodePtr head = NodePtr(initialHeadNode);
    std::atomic<unsigned long> sl_zero = std::atomic<unsigned long>(0UL);

    std::atomic<background_state> bg_state = std::atomic<background_state>(INITIAL);

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
    int internal_finish_contains(const K& key, Node *node, V *node_val, optional<V>& ret_value);
    int internal_finish_delete(const K& key, Node *node, V *node_val, optional<V>& ret_value, int tid);
    int internal_finish_insert(const K& key, V *val, Node *node, V *node_val, Node *next);
    bool internal_do_operation(operation_type optype, const K& key, V *val, optional<V>& ret_value, int tid);
public:
    LockfreeSkipList(int task_num) : tracker(task_num + 1, 100, 1000, true){
        int bg_tid = task_num;
        bg_state.store(background_state::RUNNING);
        background_thread = std::move(std::thread(&LockfreeSkipList::bg_loop, this, bg_tid));
    };
    ~LockfreeSkipList(){
        bg_state.store(background_state::FINISHED);
        background_thread.join();
    };

    optional<V> get(K key, int tid);
    optional<V> remove(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> replace(K key, V val, int tid);
};

template<class T>
class LockfreeSkipListFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new LockfreeSkipList<T,T>(gtc->task_num);
    }
};

template<class K, class V>
void LockfreeSkipList<K,V>::bg_loop(int tid){
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
int LockfreeSkipList<K,V>::bg_trav_nodes(int tid){
    Node *prev, *node, *next;
    Node *above_head = head.ptr.load(), *above_prev, *above_next;
    unsigned long zero = sl_zero.load();
    int raised = 0;

    assert(nullptr != head.ptr.load());

    tracker.start_op(tid);

    above_prev = above_next = above_head;
    prev = head.ptr.load();
    node = prev->next.ptr.load();
    if (nullptr == node)
        return 0;
    next = node->next.ptr.load();

    while (nullptr != next) {

        bool expected = false;
        if (nullptr == node->val.ptr.load()) {
            bg_remove(prev, node, tid);
            if (node->level >= 1)
                ++bg_tall_deleted;
            ++bg_deleted;
        } else if ((void *) node->val.ptr.load() != (void *) node) {
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

        if (nullptr != node->val.ptr.load() && (void *) node != (void *) node->val.ptr.load()) {
            ++bg_non_deleted;
        }
        prev = node;
        node = next;
        next = next->next.ptr.load();
    }

    tracker.end_op(tid);

    return raised;
}


template<class K, class V>
void LockfreeSkipList<K,V>::get_index_above(Node *above_head,
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
int LockfreeSkipList<K,V>::bg_raise_ilevel(int h, int tid){
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
        while ((void *) index->val.ptr.load() == (void *) index) {

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
                ((void *) index->val.ptr.load() != (void *) index &&
                nullptr != index->val.ptr.load()) ) {
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
void LockfreeSkipList<K,V>::bg_lower_ilevel(int tid){
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
void LockfreeSkipList<K,V>::bg_help_remove(Node *prev, Node *node, int tid){
    // Required before calling this function: tracker.start_op(tid) has been called,
    // and tracker.end_op(tid) hasn't been called.
    Node *n, *new_node, *prev_next;
    int retval;

    assert(nullptr != prev);
    assert(nullptr != node);

    if ((void *) node->val.ptr.load() != (void *) node || node->marker)
        return;

    n = node->next.ptr.load();
    while (nullptr == n || !n->marker) {
        new_node = alloc_marker_node(node, n);
        if(!node->next.ptr.compare_exchange_strong(n, new_node)){
            dealloc_marker_node(new_node);
        }

        assert (node->next.ptr.load() != node);

        n = node->next.ptr.load();
    }

    if (prev->next.ptr.load() != node || prev->marker)
        return;

    /* remove the nodes */
    retval = prev->next.ptr.compare_exchange_strong(node, n->next.ptr.load());
    assert (prev->next.ptr.load() != prev);

    if (retval) {
        tracker.retire(node, tid);
        tracker.retire(n, tid);
    }

    /*
     * update the prev pointer - we don't need synchronisation here
     * since the prev pointer does not need to be exact
     */
    prev_next = prev->next.ptr.load();
    if (nullptr != prev_next)
        prev_next->prev.ptr.store(prev);
}

template<class K, class V>
void LockfreeSkipList<K,V>::bg_remove(Node *prev, Node *node, int tid){
    assert(nullptr != node);

    if (0 == node->level) {
        /* only remove short nodes */

        /* CAS expects a reference as the first argument, so we create a temporary variable as a shim. */
        V *tmp = nullptr;
        node->val.ptr.compare_exchange_strong(tmp, (V *) node);
        if ((void *) node->val.ptr.load() == (void *) node)
            bg_help_remove(prev, node, tid);
    }
}

template<class K, class V>
int LockfreeSkipList<K,V>::internal_finish_contains(const K& key, Node *node, V *node_val, optional<V>& ret_value){
    int result = 0;

    assert(nullptr != node);

    if ((key == node->key) && (nullptr != node_val)) {
        ret_value = V(*node_val);
        result = 1;
    }

    return result;
}

template<class K, class V>
int LockfreeSkipList<K,V>::internal_finish_delete(const K& key, Node *node, V *node_val, optional<V>& ret_value, int tid){
    int result = -1;

    assert(nullptr != node);

    if (node->key != key)
        result = 0;
    else {
        if (nullptr != node_val) {
            /* loop until we or someone else deletes */
            while (1) {
                node_val = node->val.ptr.load();
                if (nullptr == node_val || (void *) node == (void *) node_val) {
                    result = 0;
                    break;
                }
                else if (node->val.ptr.compare_exchange_strong(node_val, nullptr)) {
                    ret_value = V(*node_val);
                    tracker.retire(node_val,tid);

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
        } else {
            /* Already logically deleted */
            result = 0;
        }
    }

    return result;
}

template<class K, class V>
int LockfreeSkipList<K,V>::internal_finish_insert(const K& key, V *val, Node *node, V *node_val, Node *next){
    int result = -1;
    Node *new_node, *temp;

    if (node->key == key) {
        if (nullptr == node_val) {
            if (node->val.ptr.compare_exchange_strong(node_val, val))
                result = 1;
        } else {
            result = 0;
        }
    } else {
        new_node = new Node(key, val, node, next, 0);
        if (node->next.ptr.compare_exchange_strong(next, new_node)) {
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
bool LockfreeSkipList<K,V>::internal_do_operation(operation_type optype, const K& key, V *val, optional<V>& ret_value, int tid){
    Node *item = nullptr, *next_item = nullptr;
    Node *node = nullptr, *next = nullptr;
    Node *local_head = head.ptr.load();
    V *node_val = nullptr, *next_val = nullptr;
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

    /* find the correct node and next */
    while (1) {
        node_val = node->val.ptr.load();
        while ((void *) node == (void *) node_val) {
            node = node->prev.ptr.load();
            node_val = node->val.ptr.load();
        }
        next = node->next.ptr.load();
        if (nullptr != next) {
            next_val = next->val.ptr.load();
            if ((void *) next_val == (void *) next) {
                bg_help_remove(node, next, tid);
                continue;
            }
        }
        if (nullptr == next || next->key > key) {
            if (CONTAINS == optype)
                result = internal_finish_contains(key, node, node_val, ret_value);
            else if (DELETE == optype)
                result = internal_finish_delete(key, node, node_val, ret_value, tid);
            else if (INSERT == optype)
                result = internal_finish_insert(key, val, node, node_val, next);
            if (-1 != result)
                break;
            continue;
        }
        node = next;
    }

    tracker.end_op(tid);

    return result;
}

template<class K, class V>
bool LockfreeSkipList<K,V>::insert(K key, V val, int tid)
{
    V* s_ptr = new V(val);
    optional<V> unused = {};
    bool success = internal_do_operation(operation_type::INSERT, key, s_ptr, unused, tid);
    if(!success){
        delete s_ptr;
    }
    return success;
}

template<class K, class V>
optional<V> LockfreeSkipList<K,V>::get(K key, int tid)
{
    optional<V> res = {};
    internal_do_operation(operation_type::CONTAINS, key, nullptr, res, tid);
    return res;
}

template<class K, class V>
optional<V> LockfreeSkipList<K,V>::remove(K key, int tid)
{
    optional<V> res;
    internal_do_operation(operation_type::DELETE, key, nullptr, res, tid);
    return res;
}

template<class K, class V>
optional<V> LockfreeSkipList<K,V>::put(K key, V val, int tid)
{
    optional<V> res;

    assert(0&&"put not implemented!");

    return res;
}

template<class K, class V>
optional<V> LockfreeSkipList<K,V>::replace(K key, V val, int tid)
{
    optional<V> res;

    assert(0&&"replace not implemented!");

    return res;
}

#endif
