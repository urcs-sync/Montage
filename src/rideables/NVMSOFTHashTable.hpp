#ifndef NVM_SOFT_HASHTABLE_HPP
#define NVM_SOFT_HASHTABLE_HPP

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

namespace NVMsoftUtils
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
    Node *before = static_cast<Node *>(NVMsoftUtils::createRef(p, expected));
    Node *after = static_cast<Node *>(NVMsoftUtils::createRef(p, newVal));
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
    return isOut(NVMsoftUtils::getState(ptr));
}

} // namespace NVMsoftUtils
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


//assume K, V are both string
template <class K, class V, size_t idxSize=1000000>
class NVMSOFTHashTable : public RMap<K,V>
{
    using state = NVMsoftUtils::state;
    using KEYTYPE = std::array<char,TESTS_KEY_SIZE>;
    using VALUETYPE = std::array<char,TESTS_VAL_SIZE>;

    /* definition of persistent node */
    class PNode : public Persistent
    {
    public:
        std::atomic<bool> validStart, validEnd, deleted;
        KEYTYPE key;
        VALUETYPE value;

        PNode(const std::string& k, const std::string& val) : validStart(false), validEnd(false), deleted(false) {
            strncpy(key.data(), k.data(), TESTS_KEY_SIZE);
            strncpy(value.data(), val.data(), TESTS_VAL_SIZE);
        }

        bool alloc()
        {
            return !this->validStart.load();
        }

        void create(bool validity)
        {
            this->validStart.store(validity, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            clwb_range_nofence(&key, sizeof(KEYTYPE));
            clwb_range_nofence(&value, sizeof(VALUETYPE));
            sfence();
            this->validEnd.store(validity, std::memory_order_release);
            clwb_obj_fence(this);
        }

        void destroy(bool validity)
        {
            this->deleted.store(validity, std::memory_order_release);
            clwb_obj_fence(this);
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
    public:
        KEYTYPE key;
        // VALUETYPE value;
        PNode *pptr;
        bool pValidity;
        std::atomic<Node *> next;

        Node(PNode *pptr_, bool pValidity_, const std::string& k) : pptr(pptr_), pValidity(pValidity_), next(nullptr) { 
            strncpy(key.data(), k.data(), TESTS_KEY_SIZE);
        }
        ~Node(){
            if(pptr!=nullptr)
                delete pptr;
        }
    }; 
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


    NVMSOFTHashTable(GlobalTestConfig* gtc) : tracker(gtc->task_num, 100, 1000, true){
        Persistent::init();
        for(size_t i=0;i<idxSize;i++){
            PNode *phead = new PNode("", "");
            heads[i] = new Node(phead, false,"");
            PNode *ptail = new PNode(std::string(1,(char)127), "");
            heads[i]->next.store(new Node(ptail, false, std::string(1,(char)127)), std::memory_order_release);
        }
    }

    ~NVMSOFTHashTable(){
        Persistent::finalize();
    }

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        Persistent::init_thread(gtc, ltc);
    }

  private:
    std::hash<K> hash_fn;
    RCUTracker tracker;
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
        TrackerHolder<RCUTracker> holder(tracker,tid);
        Node *curr = head->next.load();
        while(strncmp (curr->key.data(), key.data(),(size_t)TESTS_KEY_SIZE) < 0)
        {
            curr = NVMsoftUtils::getRef<Node>(curr->next.load());
        }
        state currState = NVMsoftUtils::getState(curr->next.load());
	    if((strncmp (curr->key.data(), key.data(),TESTS_KEY_SIZE) == 0) && ((currState == state::INSERTED) || (currState == state::INTEND_TO_DELETE))){
            ret = V(curr->pptr->value.data(), TESTS_VAL_SIZE);
        }
        return ret;
    }
    bool _insert(Node *head, K key, V value, int tid)
    {
        Node *pred, *currRef;
        state currState, predState;
        TrackerHolder<RCUTracker> holder(tracker,tid);
    retry:
        while (true)
        {   
            Node *curr = _find(head, key, &pred, &currState,tid);
            currRef = NVMsoftUtils::getRef<Node>(curr);
            predState = NVMsoftUtils::getState(curr);
            Node *resultNode;
            bool result = false;

            if (strncmp(currRef->key.data(), key.data(), TESTS_KEY_SIZE) == 0)
            {
                resultNode = currRef;
                if (currState != state::INTEND_TO_INSERT)
                    return false;
            }
            else
            {   
                PNode *newPNode = new PNode(key, value);
                bool pValid = newPNode->alloc();
                Node *newNode = new Node(newPNode, pValid, key);
                newNode->next.store(static_cast<Node *>(NVMsoftUtils::createRef(currRef, state::INTEND_TO_INSERT)), std::memory_order_relaxed);
                if (!pred->next.compare_exchange_strong(curr, static_cast<Node *>(NVMsoftUtils::createRef(newNode, predState)))){
                    delete(newNode);//no need for SMR here
                    // delete(newPNode); //deletion of PNode is handled by ~Node
                    goto retry;
                }
                resultNode = newNode;
                result = true;
            }

            resultNode->pptr->create(resultNode->pValidity);

            while (NVMsoftUtils::getState(resultNode->next.load()) == state::INTEND_TO_INSERT)
                NVMsoftUtils::stateCAS<Node>(resultNode->next, state::INTEND_TO_INSERT, state::INSERTED);

            return result;
        }
    }

    optional<V> _remove(Node *head, K key, int tid)
    {
        bool casResult =false;
        optional<V> ret{};
        Node *pred, *curr, *currRef, *succ, *succRef;
        state predState, currState;
        TrackerHolder<RCUTracker> holder(tracker,tid);
        curr = _find(head, key, &pred, &currState, tid);
        currRef = NVMsoftUtils::getRef<Node>(curr);
        predState = NVMsoftUtils::getState(curr);

        if (strncmp(currRef->key.data(), key.data(), TESTS_KEY_SIZE) != 0)
        {
            return {};
        }

        if (currState == state::INTEND_TO_INSERT || currState == state::DELETED)
        {
            return {};
        }

        while (!casResult && NVMsoftUtils::getState(currRef->next.load()) == state::INSERTED)
            casResult = NVMsoftUtils::stateCAS<Node>(currRef->next, state::INSERTED, state::INTEND_TO_DELETE);


        currRef->pptr->destroy(currRef->pValidity);

        while (NVMsoftUtils::getState(currRef->next.load()) == state::INTEND_TO_DELETE){
            ret = V(currRef->pptr->value.data(), TESTS_VAL_SIZE);
            NVMsoftUtils::stateCAS<Node>(currRef->next, state::INTEND_TO_DELETE, state::DELETED);
        }
            
        if(casResult)
            _trim(pred, curr, tid);
        return ret;
    }
    // returns clean reference in pred, ref+state of pred in return and the state of curr in the last arg
    Node *_find(Node *head, K key, Node **predPtr, state *currStatePtr, int tid)
    {
        Node *prev = head, *curr = prev->next.load(), *succ, *succRef;
        Node *currRef = NVMsoftUtils::getRef<Node>(curr);
        state prevState = NVMsoftUtils::getState(curr), cState;
        while (true)
        {
            succ = currRef->next.load();
            succRef = NVMsoftUtils::getRef<Node>(succ);
            cState = NVMsoftUtils::getState(succ);
            if (LIKELY(cState != state::DELETED))
            {
                if (UNLIKELY(strncmp(currRef->key.data(), key.data(), min((size_t)TESTS_KEY_SIZE, key.size())) >= 0)){
                    break;
                }
                prev = currRef;
                prevState = cState;
            }
            else
            {
                _trim(prev, curr, tid);
            }
            curr = NVMsoftUtils::createRef<Node>(succRef, prevState);
            currRef = succRef;
        }
        *predPtr = prev;
        *currStatePtr = cState;
        return curr;
    }
    bool _trim(Node *prev, Node *curr, int tid)
    {
        state prevState = NVMsoftUtils::getState(curr);
        Node *currRef = NVMsoftUtils::getRef<Node>(curr);
        Node *succ = NVMsoftUtils::getRef<Node>(currRef->next.load());
        succ = NVMsoftUtils::createRef<Node>(succ, prevState);
        bool result = prev->next.compare_exchange_strong(curr, succ);
        if (result){
            // RP_free(currRef->pptr);
            tracker.retire(currRef,tid);
        }
        return result;
    }
};

template<class T>
class NVMSOFTHashTableFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new NVMSOFTHashTable<T,T>(gtc);
    }
};

#endif