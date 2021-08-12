#ifndef NATARAJAN_TREE
#define NATARAJAN_TREE

#include <iostream>
#include <atomic>
#include <algorithm>
#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RMap.hpp"
#include "RCUTracker.hpp"
#include "CustomTypes.hpp"

template <class K, class V>
class NatarajanTree : public RMap<K,V>{
private:
	/* structs*/
	struct Node{
		int level;
		K key;
		V val;
		std::atomic<Node*> left;
		std::atomic<Node*> right;
		virtual ~Node(){};

		Node(K k, V v, Node* l, Node* r,int lev):level(lev),key(k),val(v),left(l),right(r){};
		Node(K k, V v, Node* l, Node* r):level(-1),key(k),val(v),left(l),right(r){};
	};
	struct SeekRecord{
		Node* ancestor;
		Node* successor;
		Node* parent;
		Node* leaf;
	};

	/* variables */
	RCUTracker tracker;
	K infK{};
	V defltV{};
	Node r{infK,defltV,nullptr,nullptr,2};
	Node s{infK,defltV,nullptr,nullptr,1};
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
		return getInfLevel(n)!=-1;
	}
	inline int getInfLevel(Node* n){
		//0 for inf0, 1 for inf1, 2 for inf2, -1 for general val
		n=getPtr(n);
		return n->level;
	}
	inline bool nodeLess(Node* n1, Node* n2){
		n1=getPtr(n1);
		n2=getPtr(n2);
		int i1=getInfLevel(n1);
		int i2=getInfLevel(n2);
		return i1<i2 || (i1==-1&&i2==-1&&n1->key<n2->key);
	}
	inline bool nodeLess(const K& key, Node* n){
		n=getPtr(n);
		int i=getInfLevel(n);
		return -1<i || (i==-1&&key<n->key);
	}
	inline bool nodeEqual(Node* n1, Node* n2){
		n1=getPtr(n1);
		n2=getPtr(n2);
		int i1=getInfLevel(n1);
		int i2=getInfLevel(n2);
		if(i1==-1&&i2==-1)
			return n1->key==n2->key;
		else
			return i1==i2;
	}
	inline bool nodeLessEqual(Node* n1, Node* n2){
		return !nodeLess(n2,n1);
	}


	/* private interfaces */
	void seek(K key, int tid);
	bool cleanup(K key, int tid);
	void retire_path(Node* start, Node* end, int tid);
	// void doRangeQuery(Node& k1, Node& k2, int tid, Node* root, std::map<K,V>& res);
public:
	NatarajanTree(int task_num): tracker(task_num, 100, 1000, true){
		r.right.store(new Node(infK,defltV,nullptr,nullptr,2),std::memory_order_relaxed);
		r.left.store(&s,std::memory_order_relaxed);
		s.right.store(new Node(infK,defltV,nullptr,nullptr,1),std::memory_order_relaxed);
		s.left.store(new Node(infK,defltV,nullptr,nullptr,0),std::memory_order_relaxed);
		records = new padded<SeekRecord>[task_num]{};
	};
	~NatarajanTree(){};

	optional<V> get(K key, int tid);
	optional<V> put(K key, V val, int tid);
	bool insert(K key, V val, int tid);
	optional<V> remove(K key, int tid);
	optional<V> replace(K key, V val, int tid);
	// std::map<K, V> rangeQuery(K key1, K key2, int& len, int tid);
};

template <class T>
class NatarajanTreeFactory : public RideableFactory{
	Rideable* build(GlobalTestConfig* gtc){
		return new NatarajanTree<T,T>(gtc->task_num);
	}
};

//-------Definition----------
template <class K, class V>
void NatarajanTree<K,V>::seek(K key, int tid){
	Node keyNode{key,defltV,nullptr,nullptr};//node to be compared
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
		if(nodeLess(&keyNode,current)){
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
bool NatarajanTree<K,V>::cleanup(K key, int tid){
	Node keyNode{key,defltV,nullptr,nullptr};//node to be compared
	bool res=false;
	
	SeekRecord* seekRecord=&(records[tid].ui);
	Node* ancestor=getPtr(seekRecord->ancestor);
	Node* successor=getPtr(seekRecord->successor);
	Node* parent=getPtr(seekRecord->parent);
	Node* leaf=getPtr(seekRecord->leaf);

	std::atomic<Node*>* successorAddr=nullptr;
	std::atomic<Node*>* childAddr=nullptr;
	std::atomic<Node*>* siblingAddr=nullptr;

	if(nodeLess(&keyNode,ancestor))
		successorAddr=&(ancestor->left);
	else
		successorAddr=&(ancestor->right);
	if(nodeLess(&keyNode,parent)){
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
		// retire everything on the path [successor, parent]:
		retire_path(successor, parent, tid);
	}
	return res;
}

// retire everything on the path [start, end]
template <class K, class V>
void NatarajanTree<K,V>::retire_path(Node* start, Node* end, int tid){
    K key = end->key;
    Node* curr = start;
    while(curr!=end){
		assert(curr != nullptr && "curr shouldn't be null");
		tracker.retire(curr, tid);
        if (nodeLess(key, curr)){
            curr = getPtr(curr->left.load());
        } else {
            curr = getPtr(curr->right.load());
        }
    }
	tracker.retire(curr, tid);
}

/* to test rangeQuery */
// template <>
// optional<int> NatarajanTree<int,int>::get(int key, int tid){
// 	int len=0;
// 	auto x = rangeQuery(key-500,key,len,tid);
// 	Node keyNode{key,defltV,nullptr,nullptr};//node to be compared
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
optional<V> NatarajanTree<K,V>::get(K key, int tid){
	Node keyNode{key,defltV,nullptr,nullptr};//node to be compared
	optional<V> res={};
	SeekRecord* seekRecord=&(records[tid].ui);
	Node* leaf=nullptr;
	tracker.start_op(tid);
	seek(key,tid);
	leaf=getPtr(seekRecord->leaf);
	if(nodeEqual(&keyNode,leaf)){
		res = leaf->val;
	}

	tracker.end_op(tid);
	return res;
}

template <class K, class V>
optional<V> NatarajanTree<K,V>::put(K key, V val, int tid){
	optional<V> res={};
	SeekRecord* seekRecord=&(records[tid].ui);

	Node* newInternal=nullptr;
	Node* newLeaf=new Node(key,val,nullptr,nullptr);//also to compare keys

	Node* parent=nullptr;
	Node* leaf=nullptr;
	std::atomic<Node*>* childAddr=nullptr;
	tracker.start_op(tid);
	while(true){
		seek(key,tid);
		leaf=getPtr(seekRecord->leaf);
		parent=getPtr(seekRecord->parent);
		if(!nodeEqual(newLeaf,leaf)){//key does not exist
			childAddr=nullptr;
			if(nodeLess(newLeaf,parent))
				childAddr=&(parent->left);
			else
				childAddr=&(parent->right);

			//set left and right of newInternal
			Node* newLeft=nullptr;
			Node* newRight=nullptr;
			if(nodeLess(newLeaf,leaf)){
				newLeft=newLeaf;
				newRight=leaf;
			}
			else{
				newLeft=leaf;
				newRight=newLeaf;
			}
			//create newInternal
			if(isInf(leaf)){
				int lev=getInfLevel(leaf);
				newInternal=new Node(infK,defltV,newLeft,newRight,lev);
			}
			else
				newInternal=new Node(std::max(key,leaf->key),defltV,newLeft,newRight);

			Node* tmpExpected=getPtr(leaf);
			if(childAddr->compare_exchange_strong(tmpExpected,getPtr(newInternal))){
				res={};
				break;//insertion succeeds
			}
			else{//fails; help conflicting delete operation
				delete(newInternal);// this is always local so no need to use tracker
				Node* tmpChild=childAddr->load();
				if(getPtr(tmpChild)==leaf && (getFlg(tmpChild)||getTg(tmpChild)))
					cleanup(key,tid);
			}
		}
		else{//key exists, update and return old
			res=leaf->val;
			if(nodeLess(newLeaf,parent))
				childAddr=&(parent->left);
			else
				childAddr=&(parent->right);
			if(childAddr->compare_exchange_strong(leaf,newLeaf)){
				tracker.retire(leaf,tid);
				break;
			}
		}
	}

	tracker.end_op(tid);
	return res;
}

template <class K, class V>
bool NatarajanTree<K,V>::insert(K key, V val, int tid){
	bool res=false;
	SeekRecord* seekRecord=&(records[tid].ui);
	
	Node* newInternal=nullptr;
	Node* newLeaf=new Node(key,val,nullptr,nullptr);//also for comparing keys
	
	Node* parent=nullptr;
	Node* leaf=nullptr;
	std::atomic<Node*>* childAddr=nullptr;
	tracker.start_op(tid);
	while(true){
		seek(key,tid);
		parent=getPtr(seekRecord->parent);
		leaf=getPtr(seekRecord->leaf);
		if(!nodeEqual(newLeaf,leaf)){//key does not exist
			childAddr=nullptr;
			if(nodeLess(newLeaf,parent))
				childAddr=&(parent->left);
			else
				childAddr=&(parent->right);

			//set left and right of newInternal
			Node* newLeft=nullptr;
			Node* newRight=nullptr;
			if(nodeLess(newLeaf,leaf)){
				newLeft=newLeaf;
				newRight=leaf;
			}
			else{
				newLeft=leaf;
				newRight=newLeaf;
			}
			//create newInternal
			if(isInf(leaf)){
				int lev=getInfLevel(leaf);
				newInternal=new Node(infK,defltV,newLeft,newRight,lev);
			}
			else
				newInternal=new Node(std::max(key,leaf->key),defltV,newLeft,newRight);

			Node* tmpExpected=getPtr(leaf);
			if(childAddr->compare_exchange_strong(tmpExpected,getPtr(newInternal))){
				res=true;
				break;
			}
			else{//fails; help conflicting delete operation
				delete(newInternal);// this is always local so no need to use tracker
				Node* tmpChild=childAddr->load();
				if(getPtr(tmpChild)==leaf && (getFlg(tmpChild)||getTg(tmpChild)))
					cleanup(key,tid);
			}
		}
		else{//key exists, insertion fails
			delete(newLeaf);// this is always local so no need to use tracker
			res=false;
			break;
		}
	}

	tracker.end_op(tid);
	return res;
}

template <class K, class V>
optional<V> NatarajanTree<K,V>::remove(K key, int tid){
	bool injecting = true;
	optional<V> res={};
	SeekRecord* seekRecord=&(records[tid].ui);

	Node keyNode{key,defltV,nullptr,nullptr};//node to be compared
	
	Node* parent=nullptr;
	Node* leaf=nullptr;
	std::atomic<Node*>* childAddr=nullptr;
	
	tracker.start_op(tid);
	while(true){
		seek(key,tid);
		parent=getPtr(seekRecord->parent);
		if(nodeLess(&keyNode,parent))
			childAddr=&(parent->left);
		else
			childAddr=&(parent->right);

		if(injecting){
			//injection mode: check if the key exists
			leaf=getPtr(seekRecord->leaf);
			if(!nodeEqual(leaf,&keyNode)){//does not exist
				res={};
				break;
			}

			Node* tmpExpected=getPtr(leaf);
			res=leaf->val;
			if(childAddr->compare_exchange_strong(tmpExpected,
				mixPtrFlgTg(tmpExpected,true,false))){
				injecting=false;
				if(cleanup(key,tid)) break;
			}
			else{
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
optional<V> NatarajanTree<K,V>::replace(K key, V val, int tid){
	optional<V> res={};
	SeekRecord* seekRecord=&(records[tid].ui);

	Node* newInternal=nullptr;
	Node* newLeaf=new Node(key,val,nullptr,nullptr);//also to compare keys

	Node* parent=nullptr;
	Node* leaf=nullptr;
	std::atomic<Node*>* childAddr=nullptr;

	tracker.start_op(tid);
	while(true){
		seek(key,tid);
		parent=getPtr(seekRecord->parent);
		leaf=getPtr(seekRecord->leaf);
		if(!nodeEqual(newLeaf,leaf)){//key does not exist, replace fails
			delete(newLeaf);// this is always local so no need to use tracker
			res={};
			break;
		}
		else{//key exists, update and return old
			res=leaf->val;
			if(nodeLess(newLeaf,parent))
				childAddr=&(parent->left);
			else
				childAddr=&(parent->right);
			if(childAddr->compare_exchange_strong(leaf,newLeaf)){
				tracker.retire(leaf,tid);
				break;
			}
		}
	}
	tracker.end_op(tid);
	return res;
}

#endif