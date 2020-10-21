#ifndef MONTAGE_NATARAJAN_TREE
#define MONTAGE_NATARAJAN_TREE

#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RMap.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"
#include "persist_struct_api.hpp"

using namespace pds;

template <class K, class V>
class MontageNatarajanTree : public RMap<K,V>{
public:
    class Payload : public PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y){}
        // Payload(const Payload& oth): PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    };
private:
    /* transient structs */
    enum Level { finite = -1, inf0 = 0, inf1 = 1, inf2 = 2};
    struct Node{
        Level level;
        std::atomic<Node*> left;
        std::atomic<Node*> right;
        K key;
        Payload* payload;// TODO: does it have to be atomic?

        Node(K k, V val, Node* l=nullptr, Node* r=nullptr):level(finite),left(l),right(r),key(k),payload(PNEW(Payload, key, val)){ };
        Node(Level lev, Node* l=nullptr, Node* r=nullptr):level(lev),left(l),right(r),key(),payload(nullptr){
            assert(lev != finite && "use constructor with another signature for concrete nodes!");
        };
        ~Node(){
            if(payload!=nullptr){
                // this is a leaf
                PRECLAIM(payload);
            }
        }

        void rm_payload(){
            // call it before END_OP but after linearization point
            assert(level == finite);
            assert(payload!=nullptr && "payload shouldn't be null");
            PRETIRE(payload);
        }
        V get_val(){
            // call it within BEGIN_OP and END_OP
            assert(payload!=nullptr && "payload shouldn't be null");
            return (V)payload->get_val();
        }
        //not thread-safe
        void set(K k, Node* l=nullptr, Node* r=nullptr){
            key = k;
            level = finite;
            left.store(l);
            right.store(r);
        }
        //not thread-safe
        void set(Level lev, Node* l=nullptr, Node* r=nullptr){
            level = lev;
            left.store(l);
            right.store(r);
        }
    };
    struct SeekRecord{
        Node* ancestor;
        Node* successor;
        Node* parent;
        Node* leaf;
    };

    /* variables */
    RCUTracker<Node> tracker;
    const V defV{};
    Node r{inf2};
    Node s{inf1};
    padded<SeekRecord>* records;
    const size_t GET_POINTER_BITS = 0xfffffffffffffffc;//for machine 64-bit or less.

    /* helper functions */
    //flag and tags helpers
    inline Node* getPtr(Node* mptr){
        return (Node*) ((size_t)mptr & GET_POINTER_BITS);
    }
    inline bool getFlg(Node* mptr){
        return (bool)((size_t)mptr & 1);
    }
    inline bool getTg(Node* mptr){
        return (bool)((size_t)mptr & 2);
    }
    inline Node* mixPtrFlgTg(Node* ptr, bool flg, bool tg){
		return (Node*) ((size_t)ptr | flg | ((size_t)tg<<1));
	}
    //node comparison
    inline bool isInf(Node* n){
        return getInfLevel(n)!=finite;
    }
    inline Level getInfLevel(Node* n){
        //0 for inf0, 1 for inf1, 2 for inf2, -1 for general val
        n=getPtr(n);
        return n->level;
    }
    inline bool nodeLess(Node* n1, Node* n2){
        n1=getPtr(n1);
        n2=getPtr(n2);
        int i1=getInfLevel(n1);
        int i2=getInfLevel(n2);
        return i1<i2 || (i1==finite&&i2==finite&&n1->key<n2->key);
    }
    inline bool nodeLess(const K& key, Node* n){
        n=getPtr(n);
        int i=getInfLevel(n);
        return finite<i || (i==finite&&key<n->key);
    }
    inline bool nodeEqual(Node* n1, Node* n2){
        n1=getPtr(n1);
        n2=getPtr(n2);
        int i1=getInfLevel(n1);
        int i2=getInfLevel(n2);
        if(i1==finite&&i2==finite)
            return n1->key==n2->key;
        else
            return i1==i2;
    }
    inline bool nodeEqual(const K& key, Node* n){
        n=getPtr(n);
        int i=getInfLevel(n);
        if(i==finite)
            return key==n->key;
        else
            return false;
    }
    inline bool nodeLessEqual(Node* n1, Node* n2){
        return !nodeLess(n2,n1);
    }

    /* private interfaces */
    void seek(K key, int tid);
    bool cleanup(K key, int tid);
    // void doRangeQuery(Node& k1, Node& k2, int tid, Node* root, std::map<K,V>& res);
public:
    MontageNatarajanTree(GlobalTestConfig* gtc): tracker(gtc->task_num, 100, 1000, true){
        // init Persistent allocator
        Persistent::init();
        // init epoch system
        pds::init(gtc);
        // init main thread
        pds::init_thread(0);

        r.right.store(new Node(inf2));
        r.left.store(&s);
        s.right.store(new Node(inf1));
        s.left.store(new Node(inf0));
        records = new padded<SeekRecord>[gtc->task_num]{};
    };
    ~MontageNatarajanTree(){};

    void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        pds::init_thread(ltc->tid);
    }

    optional<V> get(K key, int tid);
    optional<V> put(K key, V val, int tid);
    bool insert(K key, V val, int tid);
    optional<V> remove(K key, int tid);
    optional<V> replace(K key, V val, int tid);
    // std::map<K, V> rangeQuery(K key1, K key2, int& len, int tid);
};

template<class T>
class MontageNatarajanTreeFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new MontageNatarajanTree<T,T>(gtc);
    }
};

//-------Definition----------
template <class K, class V>
void MontageNatarajanTree<K,V>::seek(K key, int tid){
    SeekRecord* seekRecord=&(records[tid].ui);
    seekRecord->ancestor=&r;
    seekRecord->successor=&s;
    seekRecord->parent=&s;
    seekRecord->leaf=getPtr(s.left.load());
    
    Node* parentField=seekRecord->parent->left.load();
    Node* currentField=seekRecord->leaf->left.load();
    Node* current=getPtr(currentField);

    while(current!=nullptr){
        if(!getTg(parentField)){
            seekRecord->ancestor=seekRecord->parent;
            seekRecord->successor=seekRecord->leaf;
        }
        seekRecord->parent=seekRecord->leaf;
        seekRecord->leaf=current;

        parentField=currentField;
        if(nodeLess(key,current)){
            currentField=current->left.load();
        }
        else{
            currentField=current->right.load();
        }
        current=getPtr(currentField);
    }
    return;
}

template <class K, class V>
bool MontageNatarajanTree<K,V>::cleanup(K key, int tid){
    bool res=false;
    
    SeekRecord* seekRecord=&(records[tid].ui);
    Node* ancestor=getPtr(seekRecord->ancestor);
    Node* successor=getPtr(seekRecord->successor);
    Node* parent=getPtr(seekRecord->parent);
    Node* leaf=getPtr(seekRecord->leaf);

    std::atomic<Node*>* successorAddr=nullptr;
    std::atomic<Node*>* childAddr=nullptr;
    std::atomic<Node*>* siblingAddr=nullptr;

    if(nodeLess(key,ancestor))
        successorAddr=&(ancestor->left);
    else
        successorAddr=&(ancestor->right);
    if(nodeLess(key,parent)){
        childAddr=&(parent->left);
        siblingAddr=&(parent->right);
    }
    else{
        childAddr=&(parent->right);
        siblingAddr=&(parent->left);
    }
    Node* tmpChild=childAddr->load();
    if(!getFlg(tmpChild)){
        //the leaf is not flagged, thus sibling node should be flagged
        tmpChild=siblingAddr->load();
        siblingAddr=childAddr;
    }

    while(true){
        Node* untagged=siblingAddr->load();
        Node* tagged=mixPtrFlgTg(getPtr(untagged),getFlg(untagged),true);
        if(siblingAddr->compare_exchange_weak(untagged,tagged)){
            break;
        }
    }
    Node* tmpSibling=siblingAddr->load();
    res=successorAddr->compare_exchange_strong(successor,
        mixPtrFlgTg(getPtr(tmpSibling),getFlg(tmpSibling),false));

    if(res==true){
        tracker.retire(getPtr(tmpChild),tid);
        tracker.retire(successor,tid);
    }
    return res;
}

/* to test rangeQuery */
// template <>
// optional<int> MontageNatarajanTree<int,int>::get(int key, int tid){
// 	int len=0;
// 	auto x = rangeQuery(key-500,key,len,tid);
// 	Node keyNode{key,defV,nullptr,nullptr};//node to be compared
// 	optional<int> res={};
// 	SeekRecord* seekRecord=&(records[tid].ui);
// 	Node* leaf=nullptr;
// 	seek(key,tid);
// 	leaf=getPtr(seekRecord->leaf);
// 	if(nodeEqual(&keyNode,leaf)){
// 		res = leaf->val;
// 	}
// 	return res;
// }

template <class K, class V>
optional<V> MontageNatarajanTree<K,V>::get(K key, int tid){
    optional<V> res={};
    SeekRecord* seekRecord=&(records[tid].ui);
    Node* leaf=nullptr;
    tracker.start_op(tid);
    seek(key,tid);
    leaf=getPtr(seekRecord->leaf);
    if(nodeEqual(key,leaf)){
        BEGIN_OP_AUTOEND();
        res = leaf->get_val();//never old see new as we find node before BEGIN_OP
    }

    tracker.end_op(tid);
    return res;
}

template <class K, class V>
optional<V> MontageNatarajanTree<K,V>::put(K key, V val, int tid){
    optional<V> res={};
    SeekRecord* seekRecord=&(records[tid].ui);

    Node* newInternal=new Node(inf2);
    Node* newLeaf=new Node(key,val);

    Node* parent=nullptr;
    Node* leaf=nullptr;
    std::atomic<Node*>* childAddr=nullptr;
    tracker.start_op(tid);
    while(true){
        seek(key,tid);
        leaf=getPtr(seekRecord->leaf);
        parent=getPtr(seekRecord->parent);
        if(!nodeEqual(key,leaf)){//key does not exist
            childAddr=nullptr;
            if(nodeLess(key,parent))
                childAddr=&(parent->left);
            else
                childAddr=&(parent->right);

            //set left and right of newInternal
            Node* newLeft=nullptr;
            Node* newRight=nullptr;
            if(nodeLess(key,leaf)){
                newLeft=newLeaf;
                newRight=leaf;
            }
            else{
                newLeft=leaf;
                newRight=newLeaf;
            }
            //create newInternal
            if(isInf(leaf)){
                Level lev=getInfLevel(leaf);
                newInternal->set(lev,newLeft,newRight);
            }
            else
                newInternal->set(std::max(key,leaf->key),newLeft,newRight);

            Node* tmpExpected=getPtr(leaf);
            BEGIN_OP(newLeaf->payload);
            if(childAddr->compare_exchange_strong(tmpExpected,getPtr(newInternal))){
                END_OP;
                res={};
                break;//insertion succeeds
            }
            else{//fails; help conflicting delete operation
                ABORT_OP;
                Node* tmpChild=childAddr->load();
                if(getPtr(tmpChild)==leaf && (getFlg(tmpChild)||getTg(tmpChild)))
                    cleanup(key,tid);
            }
            
        }
        else{//key exists, update and return old
            if(nodeLess(key,parent))
                childAddr=&(parent->left);
            else
                childAddr=&(parent->right);
            BEGIN_OP(newLeaf->payload);
            res=leaf->get_val();
            if(childAddr->compare_exchange_strong(leaf,newLeaf)){
                leaf->rm_payload();
                END_OP;
                delete(newInternal);// this is always local so no need to use tracker
                tracker.retire(leaf,tid);
                break;
            }
            ABORT_OP;
        }
    }

    tracker.end_op(tid);
    // assert(0&&"put isn't implemented");
    return res;
}

template <class K, class V>
bool MontageNatarajanTree<K,V>::insert(K key, V val, int tid){
    bool res=false;
    SeekRecord* seekRecord=&(records[tid].ui);
    
    Node* newInternal=new Node(inf2);
    Node* newLeaf=new Node(key,val);
    
    Node* parent=nullptr;
    Node* leaf=nullptr;
    std::atomic<Node*>* childAddr=nullptr;
    tracker.start_op(tid);
    while(true){
        seek(key,tid);
        parent=getPtr(seekRecord->parent);
        leaf=getPtr(seekRecord->leaf);
        if(!nodeEqual(key,leaf)){//key does not exist
            childAddr=nullptr;
            if(nodeLess(key,parent))
                childAddr=&(parent->left);
            else
                childAddr=&(parent->right);

            //set left and right of newInternal
            Node* newLeft=nullptr;
            Node* newRight=nullptr;
            if(nodeLess(key,leaf)){
                newLeft=newLeaf;
                newRight=leaf;
            }
            else{
                newLeft=leaf;
                newRight=newLeaf;
            }
            //create newInternal
            if(isInf(leaf)){
                Level lev=getInfLevel(leaf);
                newInternal->set(lev,newLeft,newRight);
            }
            else
                newInternal->set(std::max(key,leaf->key),newLeft,newRight);

            Node* tmpExpected=getPtr(leaf);
            BEGIN_OP(newLeaf->payload);
            if(childAddr->compare_exchange_strong(tmpExpected,getPtr(newInternal))){
                END_OP;
                res=true;
                break;
            }
            else{//fails; help conflicting delete operation
                ABORT_OP;
                Node* tmpChild=childAddr->load();
                if(getPtr(tmpChild)==leaf && (getFlg(tmpChild)||getTg(tmpChild)))
                    cleanup(key,tid);
            }
        }
        else{//key exists, insertion fails
            delete(newInternal);// this is always local so no need to use tracker
            delete(newLeaf);// this is always local so no need to use tracker
            res=false;
            break;
        }
    }

    tracker.end_op(tid);
    return res;
}

template <class K, class V>
optional<V> MontageNatarajanTree<K,V>::remove(K key, int tid){
    bool injecting = true;
    optional<V> res={};
    SeekRecord* seekRecord=&(records[tid].ui);

    Node* parent=nullptr;
    Node* leaf=nullptr;
    std::atomic<Node*>* childAddr=nullptr;
    
    tracker.start_op(tid);
    while(true){
        seek(key,tid);
        parent=getPtr(seekRecord->parent);
        if(nodeLess(key,parent))
            childAddr=&(parent->left);
        else
            childAddr=&(parent->right);

        if(injecting){
            //injection mode: check if the key exists
            leaf=getPtr(seekRecord->leaf);
            if(!nodeEqual(key,leaf)){//does not exist
                res={};
                break;
            }

            Node* tmpExpected=leaf;
            BEGIN_OP();
            res=leaf->get_val();
            if(childAddr->compare_exchange_strong(tmpExpected,
                mixPtrFlgTg(tmpExpected,true,false))){
                /* 
                 * there won't be old_see_new because insert must happen 
                 * before the leaf is found which is of course before that BEGIN_OP
                 */
                leaf->rm_payload();
                END_OP;
                injecting=false;
                if(cleanup(key,tid)) break;
            }
            else{
                ABORT_OP;
                Node* tmpChild=childAddr->load();
                if(getPtr(tmpChild)==leaf && (getFlg(tmpChild)||getTg(tmpChild)))
                    cleanup(key,tid);
            }
        }
        else{
            //cleanup mode: check if flagged node still exists
            if(seekRecord->leaf!=leaf){
                break;
            }
            else{
                if(cleanup(key,tid)) break;
            }
        }
    }
    tracker.end_op(tid);
    return res;
}

template <class K, class V>
optional<V> MontageNatarajanTree<K,V>::replace(K key, V val, int tid){
    optional<V> res={};
    // SeekRecord* seekRecord=&(records[tid].ui);

    // Node* newLeaf=new Node(key,val);

    // Node* parent=nullptr;
    // Node* leaf=nullptr;
    // std::atomic<Node*>* childAddr=nullptr;

    // tracker.start_op(tid);
    // while(true){
    //     seek(key,tid);
    //     parent=getPtr(seekRecord->parent);
    //     leaf=getPtr(seekRecord->leaf);
    //     if(!nodeEqual(key,leaf)){//key does not exist, replace fails
    //         delete(newLeaf->payload);// payload is useless, directly reclaiming
    //         delete(newLeaf);// this is always local so no need to use tracker
    //         res={};
    //         break;
    //     }
    //     else{//key exists, update and return old
    //         if(nodeLess(key,parent))
    //             childAddr=&(parent->left);
    //         else
    //             childAddr=&(parent->right);
    //         BEGIN_OP(newLeaf->payload);
    //         res=leaf->get_val();
    //         if(childAddr->compare_exchange_strong(leaf,newLeaf)){
    //             leaf->rm_payload();
    //             END_OP;
    //             tracker.retire(leaf,tid);
    //             break;
    //         }
    //         ABORT_OP;
    //     }
    // }
    // tracker.end_op(tid);
    assert(0&&"replace isn't implemented");
    return res;
}

/* Specialization for strings */
#include <string>
#include "PString.hpp"
template <>
class MontageNatarajanTree<std::string, std::string>::Payload : public PBlk{
    GENERATE_FIELD(PString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(PString<TESTS_VAL_SIZE>, val, Payload);

public:
    Payload(std::string k, std::string v) : m_key(this, k), m_val(this, v){}
    void persist(){}
};

#endif
