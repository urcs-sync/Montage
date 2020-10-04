#ifndef SOFT_HASHTABLE_HPP
#define SOFT_HASHTABLE_HPP

#include <atomic>
#include "RCUTracker.hpp"
#include <cmath>
#include <string>
#include <cstring>
#include <functional>
#include <iostream>
#include <array>
#include <optional>
#include "TestConfig.hpp"
#include "RMap.hpp"
#include "ConcurrentPrimitives.hpp"
#include "PersistFunc.hpp"

namespace softUtils
{

const int KEYSIZE = TESTS_KEY_SIZE;
const int VALUESIZE = TESTS_VAL_SIZE;
const int STATE_MASK = 0x3;
enum state
{
    INSERTED = 0,
    INTEND_TO_DELETE = 1,
    INTEND_TO_INSERT = 2,
    DELETED = 3
};

template <class Node>
static inline Node *getRef(Node *ptr)
{
    auto ptrLong = (uintptr_t)(ptr);
    ptrLong &= ~STATE_MASK;
    return (Node *)(ptrLong);
}

template <class Node>
static inline Node *createRef(Node *p, state s)
{
    auto ptrLong = (uintptr_t)(p);
    ptrLong &= ~STATE_MASK;
    ptrLong |= s;
    return (Node *)(ptrLong);
}

template <class Node>
static inline bool stateCAS(std::atomic<Node *> &atomicP, state expected, state newVal)
{
    Node *p = atomicP.load();
    Node *before = static_cast<Node *>(softUtils::createRef(p, expected));
    Node *after = static_cast<Node *>(softUtils::createRef(p, newVal));
    return atomicP.compare_exchange_strong(before, after);
}

static inline state getState(void *p)
{
    return static_cast<state>((uintptr_t)(p)&STATE_MASK);
}

static inline bool isOut(state s)
{
    return s == state::DELETED || s == state::INTEND_TO_INSERT;
}

static inline bool isOut(void *ptr)
{
    return isOut(softUtils::getState(ptr));
}

} // namespace softUtils
typedef softUtils::state state;
using namespace persist_func;

// namespace std{
//     template<size_t sz>
//     struct hash<std::array<char, sz>>{
//         int operator()(const std::array<char, sz>& arr) const{
//             std::string str(std::begin(arr), std::end(arr));
//             return std::hash<std::string>{}(str);
//         }
//     };
// }

/* definition of persistent node */
class PNode : public Persistent
{
    using K = std::array<char,softUtils::KEYSIZE>;
    using V = std::array<char,softUtils::VALUESIZE>;
  public:
    std::atomic<bool> validStart, validEnd, deleted;
    K key;
    V value;

    PNode() : validStart(false), validEnd(false), deleted(false) {}

	bool alloc()
	{
		return !this->validStart.load();
	}

	void create(const K& k, const V& val, bool validity)
	{
        this->validStart.store(validity, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		key=k;
        value=val;
		clwb_range(&key, sizeof(K));
		clwb_range(&value, sizeof(V));
		this->validEnd.store(validity, std::memory_order_release);
		flush_fence(this);
	}

	void destroy(bool validity)
	{
		this->deleted.store(validity, std::memory_order_release);
		flush_fence(this);
	}

	bool isValid()
	{
		return validStart.load() == validEnd.load() && validEnd.load() != deleted.load();
	}

	bool isDeleted()
	{
		return validStart.load() == validEnd.load() && validEnd.load() == deleted.load();
	}

	bool recoveryValidity()
	{
		return validStart.load();
	}

} __attribute__((aligned((64))));

/* definition of volatile node */
// template <class K, class V>
class Node
{
    using K = std::array<char,softUtils::KEYSIZE>;
    using V = std::array<char,softUtils::VALUESIZE>;
public:
    K key;
    V value;
    PNode *pptr;
    bool pValidity;
    std::atomic<Node *> next;

    Node(const std::string& key_, const std::string& value_, PNode *pptr_, bool pValidity_) : pptr(pptr_), pValidity(pValidity_), next(nullptr) {
        memcpy(key.data(), key_.data(), key_.size());
        if (key_.size()<softUtils::KEYSIZE)
            key[key_.size()] = '\0';
        memcpy(value.data(), value_.data(), value_.size());
        if (value_.size()<softUtils::VALUESIZE)
            value[value_.size()] = '\0';
    }
    ~Node(){
        if(pptr!=nullptr)
            delete pptr;
    }
}; 

//assume K, V are both string
template <class K, class V, size_t idxSize=1000000>
class SOFTHashTable : public RMap<K,V>
{
    using KEYTYPE = std::array<char,softUtils::KEYSIZE>;
    using VALUETYPE = std::array<char,softUtils::VALUESIZE>;
public:
    bool insert(K k, V item, int tid)
    {   
        auto bucket = getBucket(k);
        return _insert(bucket, k, item, tid);
    }

    optional<V> put(K k, V item, int tid){
        std::cerr<<"function not implemented!\n";
        exit(1);
        return {};
    }
    optional<V> replace(K key, V val, int tid){
        std::cerr<<"function not implemented!\n";
        exit(1);
        return {};
    }

    optional<V> remove(K k, int tid)
    {
        auto bucket = getBucket(k);
        return _remove(bucket, k, tid);
    }

    optional<V> get(K k, int tid)
    {
        auto bucket = getBucket(k);
        return _get(bucket, k, tid);
    }


    SOFTHashTable(GlobalTestConfig* gtc) : tracker(gtc->task_num, 100, 1000, true){
        for(size_t i=0;i<idxSize;i++){
            heads[i] = new Node("", "", nullptr, false);
            heads[i]->next.store(new Node(std::string(1,(char)127), "", nullptr, false), std::memory_order_release);
        }
    };

  private:
    std::hash<K> hash_fn;
    RCUTracker<Node> tracker;
    Node *getBucket(K k)
    {
        int idx = hash_fn(k) % idxSize;
        return heads[std::abs(idx)];
    }

    // SOFTList<K,V> table[idxSize];
    Node *heads[idxSize];

    optional<V> _get(Node *head, K key, int tid)
    {   
        optional<V> ret{};
        TrackerHolder<RCUTracker<Node>> holder(tracker,tid);
        Node *curr = head->next.load();
        while(strncmp (curr->key.data(), key.data(),min((size_t)softUtils::KEYSIZE,key.length())) < 0)
        {
            curr = softUtils::getRef<Node>(curr->next.load());
        }
        state currState = softUtils::getState(curr->next.load());
	    if((strncmp (curr->key.data(), key.data(),min((size_t)softUtils::KEYSIZE,key.size())) == 0) && ((currState == state::INSERTED) || (currState == state::INTEND_TO_DELETE))){
            ret = V(curr->value.data(), softUtils::VALUESIZE);
        }
        return ret;
    }
    bool _insert(Node *head, K key, V value, int tid)
    {
        Node *pred, *currRef;
        state currState, predState;
        TrackerHolder<RCUTracker<Node>> holder(tracker,tid);
    retry:
        while (true)
        {   
            Node *curr = _find(head, key, &pred, &currState,tid);
            currRef = softUtils::getRef<Node>(curr);
            predState = softUtils::getState(curr);
            Node *resultNode;
            bool result = false;

            if (strncmp(currRef->key.data(), key.data(), min((size_t)softUtils::KEYSIZE, key.size())) == 0)
            {
                resultNode = currRef;
                if (currState != state::INTEND_TO_INSERT)
                    return false;
            }
            else
            {   
                PNode *newPNode = new PNode();
                bool pValid = newPNode->alloc();
                Node *newNode = new Node(key, value, newPNode, pValid);
                newNode->next.store(static_cast<Node *>(softUtils::createRef(currRef, state::INTEND_TO_INSERT)), std::memory_order_relaxed);
                if (!pred->next.compare_exchange_strong(curr, static_cast<Node *>(softUtils::createRef(newNode, predState)))){
                    delete(newNode);//no need for SMR here
                    // delete(newPNode); //deletion of PNode is handled by ~Node
                    goto retry;
                }
                resultNode = newNode;
                result = true;
            }

            resultNode->pptr->create(resultNode->key, resultNode->value, resultNode->pValidity);

            while (softUtils::getState(resultNode->next.load()) == state::INTEND_TO_INSERT)
                softUtils::stateCAS<Node>(resultNode->next, state::INTEND_TO_INSERT, state::INSERTED);

            return result;
        }
    }

    optional<V> _remove(Node *head, K key, int tid)
    {
        bool casResult =false;
        optional<V> ret{};
        Node *pred, *curr, *currRef, *succ, *succRef;
        state predState, currState;
        TrackerHolder<RCUTracker<Node>> holder(tracker,tid);
        curr = _find(head, key, &pred, &currState, tid);
        currRef = softUtils::getRef<Node>(curr);
        predState = softUtils::getState(curr);

        if (strncmp(currRef->key.data(), key.data(), min((size_t)softUtils::KEYSIZE, key.size())) != 0)
        {
            return {};
        }

        if (currState == state::INTEND_TO_INSERT || currState == state::DELETED)
        {
            return {};
        }

        while (!casResult && softUtils::getState(currRef->next.load()) == state::INSERTED)
            casResult = softUtils::stateCAS<Node>(currRef->next, state::INSERTED, state::INTEND_TO_DELETE);


        currRef->pptr->destroy(currRef->pValidity);

        while (softUtils::getState(currRef->next.load()) == state::INTEND_TO_DELETE){
            ret = V(currRef->value.data(), softUtils::VALUESIZE);
            softUtils::stateCAS<Node>(currRef->next, state::INTEND_TO_DELETE, state::DELETED);
        }
            
        if(casResult)
            _trim(pred, curr, tid);
        return ret;
    }
    // returns clean reference in pred, ref+state of pred in return and the state of curr in the last arg
    Node *_find(Node *head, K key, Node **predPtr, state *currStatePtr, int tid)
    {
        Node *prev = head, *curr = prev->next.load(), *succ, *succRef;
        Node *currRef = softUtils::getRef<Node>(curr);
        state prevState = softUtils::getState(curr), cState;
        while (true)
        {
            succ = currRef->next.load();
            succRef = softUtils::getRef<Node>(succ);
            cState = softUtils::getState(succ);
            if (LIKELY(cState != state::DELETED))
            {
                if (UNLIKELY(strncmp(currRef->key.data(), key.data(), min((size_t)softUtils::KEYSIZE, key.size())) >= 0)){
                    break;
                }
                prev = currRef;
                prevState = cState;
            }
            else
            {
                _trim(prev, curr, tid);
            }
            curr = softUtils::createRef<Node>(succRef, prevState);
            currRef = succRef;
        }
        *predPtr = prev;
        *currStatePtr = cState;
        return curr;
    }
    bool _trim(Node *prev, Node *curr, int tid)
    {
        state prevState = softUtils::getState(curr);
        Node *currRef = softUtils::getRef<Node>(curr);
        Node *succ = softUtils::getRef<Node>(currRef->next.load());
        succ = softUtils::createRef<Node>(succ, prevState);
        bool result = prev->next.compare_exchange_strong(curr, succ);
        if (result){
            // RP_free(currRef->pptr);
            tracker.retire(currRef,tid);
        }
        return result;
    }
};

template<class T>
class SOFTHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new SOFTHashTable<T,T>(gtc);
    }
};

#endif