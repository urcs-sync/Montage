#ifndef PERSIST_TRACKERS_HPP
#define PERSIST_TRACKERS_HPP

#include <cstdint>
#include <atomic>
#include <list>
#include <climits>
#include <vector>

#include "common_macros.hpp"
#include "ConcurrentPrimitives.hpp"

class PersistTracker{
public:
    virtual void first_write_on_new_epoch(uint64_t e, int tid) = 0;
    virtual void after_persist_epoch(uint64_t e, int tid) = 0;
    // virtual uint64_t min_persisted() = 0;
    virtual int next_thread_to_persist(uint64_t e, int tid) = 0;
    virtual int next_thread_to_persist(uint64_t e) = 0;
    virtual uint64_t next_epoch_to_persist(int tid) = 0;
    virtual ~PersistTracker(){}
};

// Original paper:
// https://ieeexplore.ieee.org/abstract/document/6681590

// A O(log n) tree-based approach to get a minimal epoch number of all threads
// Can be used at the end of epochs to decide which threads need to be persisted

// The original proposal used LL/SC for atomic updates of marked values,
// here we stuck a monotomically increasing counter to solve the ABA problem.

// Contains per-thread TO-BE-PERSISTED epoch information
class Mindicator: public PersistTracker{
    class MarkedVal{
        friend struct Node;
        friend class Mindicator;
        uint64_t v = UINT64_MAX;
        uint64_t mark = 0; // for ABA problem. use the last bit for dirty bit.
    public:
        MarkedVal() noexcept : v(NULL_EPOCH){}
        MarkedVal(uint64_t x) noexcept : v(x){}
        MarkedVal(uint64_t x, bool dirty) noexcept : v(x), mark(dirty? 0x1ULL : 0){}
        MarkedVal(const MarkedVal& oth) noexcept = default; // trivially copyable.
        inline bool is_dirty(){
            return ((mark & 0x1ULL) == 0x1ULL);
        }
        inline void set_dirty(){
            mark |= 0x1ULL;
        }
        inline void set_clean(){
            mark &= ~0x1ULL;
        }
        inline uint64_t get_val(){
            return v;
        }
        inline void set_val(uint64_t x){
            v = x;
        }
        inline uint64_t get_counter(){
            return (mark & ~0x1ULL);
        }
        inline void prepare_mark(MarkedVal& old){
            mark &= 0x1ULL;
            mark |= old.get_counter() + 2;
        }
    };
    struct Node{
        std::atomic<MarkedVal> marked_val;
        Node* children[2];
        int seq = -1;
        int leaf_idx = -1;
        Node(){
            children[0] = nullptr;
            children[1] = nullptr;
        }
        bool CAS_val(MarkedVal& old, MarkedVal target){
            target.prepare_mark(old);
            return marked_val.compare_exchange_strong(old, target);
        }
        virtual ~Node(){}
    }__attribute__((aligned(CACHE_LINE_SIZE)));

    void init_subtree(Node* curr, std::vector<Node*>& p, int seq){
        if (!curr){
            return;
        }
        curr->seq = seq;
        curr->marked_val.store({UINT64_MAX, 0});
        if (curr->leaf_idx >= 0) { //leaf
            paths[curr->leaf_idx] = p;
            // paths[curr->leaf_idx].push_back(curr);
        } else {
            p.push_back(curr);
            init_subtree(curr->children[0], p, 0);
            init_subtree(curr->children[1], p, 1);
            p.pop_back();
        }
    }
    void reclaim_tree(Node* n){
        if (n->leaf_idx < 0){
            reclaim_tree(n->children[0]);
            reclaim_tree(n->children[1]);
            delete n;
        }
    }

    void clean(uint64_t val, Node* n){
        MarkedVal x = n->marked_val.load();
        if (x.get_val() == val && x.is_dirty()){
            n->CAS_val(x, {val, false});
        }
    }
    bool propagate(uint64_t val, Node* n){
        while(true){
            MarkedVal x = n->marked_val.load();
            // case 1: continue propagation.
            if (x.get_val() > val){
                if (n->CAS_val(x, {val, true})){
                    return false;
                }
            }
            // case 2: continue propagation.
            else if (x.is_dirty()){
                return false;
            }
            // case 3: stop propagation.
            else if (n->CAS_val(x, x)){
                return true;
            }
        }
    }
    bool summarize(uint64_t val, Node* n){ // relaxed version.
        while(true){
            MarkedVal x = n->marked_val.load();
            if (x.is_dirty()){
                return (x.get_val() < val);
            }
            uint64_t children_min = std::min(
                n->children[0]->marked_val.load().get_val(),
                n->children[1]->marked_val.load().get_val());
            if (children_min < x.get_val() && children_min < val){
                return true;
            }
            if (n->CAS_val(x, {children_min, (children_min < x.get_val())})){
                return x.get_val() < val;
            }
        }
    }
    void arrive(uint64_t val, int tid){
        // propagate stage
        int tp = paths[tid].size()-1;
        bool done = false;
        while(tp >= 0 && !done){
            done = propagate(val, paths[tid][tp]);
            tp--;
        }
        // clean stage
        unsigned i = tp+1;
        while(i < paths[tid].size()){
            clean(val, paths[tid][i]);
            i++;
        }
    }
    void depart(uint64_t val, int tid){
        int tp = paths[tid].size()-1;
        bool done = false;
        while(tp >= 0 && !done){
            done = summarize(val, paths[tid][tp]);
            tp--;
        }
    }
    
    int find_next_thread_le(Node* n, uint64_t val){
        if (n->marked_val.load().get_val() > val){
            return -1;
        }
        if (n->leaf_idx >= 0){
            return n->leaf_idx;
        } else {
            int ret = find_next_thread_le(n->children[0], val);
            if (ret >= 0){
                return ret;
            } else {
                return find_next_thread_le(n->children[1], val);
            }
        }
    }

    Node* root = nullptr;
    Node* leaves = nullptr;
    std::vector<Node*>* paths = nullptr;
    paddedAtomic<int>* has_write_op[EPOCH_WINDOW];
public:
    Mindicator(int actual_thread_cnt){
        assert(actual_thread_cnt > 0);
        // init has_write_op flags:
        for (int i = 0; i < EPOCH_WINDOW; i++){
            has_write_op[i] = new paddedAtomic<int>[actual_thread_cnt];
            for (int j = 0; j < actual_thread_cnt; j++){
                has_write_op[i][j].ui.store(0);
            }
        }
        // always use thread_cnt >= 2 to make sure tree is full.
        int thread_cnt = actual_thread_cnt == 1? 2 : actual_thread_cnt;
        leaves = new Node[thread_cnt];
        for (int i = 0; i < thread_cnt; i++){
            leaves[i].leaf_idx = i;
        }
        paths = new std::vector<Node*>[thread_cnt];
        // build a binary tree out of all the leaves.
        root = new Node();
        std::list<Node**> buffer = {&root->children[0], &root->children[1]};
        while((int)buffer.size() < (thread_cnt>>1)){
            // add a whole layer of internal nodes
            int curr_layer_size = buffer.size();
            for (int i = 0; i < curr_layer_size; i++){
                Node* new_node = new Node();
                *buffer.front() = new_node;
                buffer.push_back(&new_node->children[0]);
                buffer.push_back(&new_node->children[1]);
                buffer.pop_front();
            }
        }
        // last layer of internal nodes
        int last_layer_size = thread_cnt - buffer.size();
        std::list<Node**> tail;
        for (int i = 0; i < last_layer_size; i++){
            Node* new_node = new Node();
            *buffer.back() = new_node;
            buffer.pop_back();
            tail.push_front(&new_node->children[1]);
            tail.push_front(&new_node->children[0]);
        }
        buffer.insert(buffer.end(), tail.begin(), tail.end());
        // link all leaves
        assert((int)buffer.size() == thread_cnt || buffer.size() == 2);
        for (int i = 0; i < thread_cnt; i++){
            *buffer.front() = &leaves[i];
            buffer.pop_front();
        }
        if (actual_thread_cnt == 1){
            root->children[1]->marked_val = {UINT64_MAX, 0};
        }
        // traverse the tree and init the tree.
        std::vector<Node*> path;
        init_subtree(root, path, -1);
    }
    ~Mindicator(){
        delete paths;
        reclaim_tree(root);
        delete leaves;
    }
    void change(uint64_t val, int tid){
        // if val gets larger, depart is local;
        // otherwise, arrive is local.
        auto old_marked = leaves[tid].marked_val.load();
        if (old_marked.get_val() == val){
            return;
        }
        while(true){
            MarkedVal new_val(val);
            new_val.prepare_mark(old_marked);
            if (leaves[tid].CAS_val(old_marked, new_val)){
                break;
            }
        }
        arrive(val, tid);
        depart(old_marked.get_val(), tid);
        // std::cout<<"top now:"<<root->marked_val.load().get_val()<<std::endl;
    }
    // uint64_t min_persisted(){
    //     return root->marked_val.load().get_val();
    // }
    void first_write_on_new_epoch(uint64_t e, int tid){
        int zero = 0;
        // this may lead to false positive: a delayed write in e may update for
        // epoch e+4n, but it's OK.
        has_write_op[e%EPOCH_WINDOW][tid].ui.compare_exchange_strong(zero, 1);
    }
    void after_persist_epoch(uint64_t e, int tid){
        if (has_write_op[(e+1)%EPOCH_WINDOW][tid].ui.load() > 0){
            change(e+1, tid);
        } else {
            change(UINT64_MAX, tid);
        }
        int one = 1;
        has_write_op[e%EPOCH_WINDOW][tid].ui.compare_exchange_strong(one, 0); // TODO: ABA problem.
    }
    int next_thread_to_persist(uint64_t val, int curr){
        if (leaves[curr].marked_val.load().get_val() <= val){
            return curr;
        }
        int from = leaves[curr].seq;
        for (int i = paths[curr].size()-2; i >= 0; i--){
            Node* p = paths[curr][i];
            if (p->marked_val.load().get_val() < val){
                int ret = find_next_thread_le(p->children[1-from], val);
                if (ret >= 0){
                    return ret;
                }
            }
            from = p->seq;
        }
        return -1;
    }
    int next_thread_to_persist(uint64_t val){
        while(root->marked_val.load().get_val() <= val){
            int ret = find_next_thread_le(root, val);
            if (ret >= 0){
                return ret;
            }
        }
        return -1;
    }
    uint64_t next_epoch_to_persist(int tid){
        uint64_t ret = leaves[tid].marked_val.load().get_val();
        if (ret == UINT64_MAX){
            return NULL_EPOCH;
        }
        return ret;
    }
};

// Contains per-thread TO-BE-PERSISTED epoch information
class IncreasingMindicator : public PersistTracker{
    struct Node{
        std::atomic<uint64_t> val;
        int seq = -1;
        int leaf_idx = -1;
        Node* children[2];
        Node(){
            val.store(NULL_EPOCH);
            children[0] = nullptr;
            children[1] = nullptr;
        }
        virtual ~Node(){}
    }__attribute__((aligned(CACHE_LINE_SIZE)));

    void init_subtree(Node* curr, std::vector<Node*>& p, int seq){
        if (!curr){
            return;
        }
        curr->seq = seq;
        if (curr->leaf_idx >= 0) { //leaf
            paths[curr->leaf_idx] = p;
            paths[curr->leaf_idx].push_back(curr);
        } else {
            p.push_back(curr);
            init_subtree(curr->children[0], p, 0);
            init_subtree(curr->children[1], p, 1);
            p.pop_back();
        }
    }
    void reclaim_tree(Node* n){
        if (n->leaf_idx < 0){
            reclaim_tree(n->children[0]);
            reclaim_tree(n->children[1]);
            delete n;
        }
    }
    bool propagate(uint64_t& val, Node* n, int from){
        assert(from == 0 || from == 1);
        uint64_t old_val = n->val.load();
        while(true){
            uint64_t sibling_val = n->children[1-from]->val.load();
            uint64_t new_min = std::min(val, sibling_val);
            if (new_min > old_val){
                if (n->val.compare_exchange_strong(old_val, new_min)){
                    // either our value or our sibling changed the current value.
                    // continue propagation.
                    val = new_min;
                    return false;
                } else {
                    // else, the value is changed;
                    // CAS automatically re-reads the new value and try again.
                    val = n->children[from]->val.load();
                }
            } else {
                // didn't change the current value, or the current value is ahead of us.
                // stop propogation.
                return true;
            }
        }
    }
    int find_next_thread_le(Node* n, uint64_t val){
        if (n->val.load() > val){
            return -1;
        }
        if (n->leaf_idx >= 0){
            return n->leaf_idx;
        } else {
            int ret = find_next_thread_le(n->children[0], val);
            if (ret >= 0){
                return ret;
            } else {
                return find_next_thread_le(n->children[1], val);
            }
        }
    }

    Node* leaves = nullptr;
    Node* root = nullptr;
    std::vector<Node*>* paths = nullptr;
public:
    IncreasingMindicator(int actual_thread_cnt){
        assert(actual_thread_cnt > 0);
        int thread_cnt = actual_thread_cnt == 1? 2 : actual_thread_cnt;
        leaves = new Node[thread_cnt];
        for (int i = 0; i < thread_cnt; i++){
            leaves[i].leaf_idx = i;
        }
        paths = new std::vector<Node*>[thread_cnt];
        // build a binary tree out of all the leaves.
        root = new Node();
        std::list<Node**> buffer = {&root->children[0], &root->children[1]};
        while((int)buffer.size() < (thread_cnt>>1)){
            // add a whole layer of internal nodes
            int curr_layer_size = buffer.size();
            for (int i = 0; i < curr_layer_size; i++){
                Node* new_node = new Node();
                *buffer.front() = new_node;
                buffer.push_back(&new_node->children[0]);
                buffer.push_back(&new_node->children[1]);
                buffer.pop_front();
            }
        }
        // last layer of internal nodes
        int last_layer_size = thread_cnt - buffer.size();
        std::list<Node**> tail;
        for (int i = 0; i < last_layer_size; i++){
            Node* new_node = new Node();
            *buffer.back() = new_node;
            buffer.pop_back();
            tail.push_front(&new_node->children[1]);
            tail.push_front(&new_node->children[0]);
        }
        buffer.insert(buffer.end(), tail.begin(), tail.end());
        // link all leaves
        assert((int)buffer.size() == thread_cnt || buffer.size() == 2);
        for (int i = 0; i < thread_cnt; i++){
            *buffer.front() = &leaves[i];
            buffer.pop_front();
        }
        if (actual_thread_cnt == 1){
            root->children[1]->val = UINT64_MAX;
        }
        // traverse the tree and init the tree.
        std::vector<Node*> path;
        init_subtree(root, path, -1);
    }
    ~IncreasingMindicator(){
        delete paths;
        reclaim_tree(root);
        delete leaves;
    }
    void first_write_on_new_epoch(uint64_t e, int tid){
        // do nothing.
    }
    void after_persist_epoch(uint64_t val, int tid){
        uint64_t old_val = leaves[tid].val.load();
        while(true){
            if (old_val > val){
                return;
            }
            if (leaves[tid].val.compare_exchange_strong(old_val, val+1)){
                break;
            }
        }
        for (int i = paths[tid].size()-2; i >= 0; i--){
            if (propagate(val, paths[tid][i], paths[tid][i+1]->seq)){
                return;
            }
        }
    }
    int next_thread_to_persist(uint64_t val){
        while(root->val.load() <= val){
            int ret = find_next_thread_le(root, val);
            if (ret >= 0){
                return ret;
            }
        }
        return -1;
    }
    int next_thread_to_persist(uint64_t val, int curr){
        if (leaves[curr].val.load() <= val){
            return curr;
        }
        int from = leaves[curr].seq;
        for (int i = paths[curr].size()-2; i >= 0; i--){
            Node* p = paths[curr][i];
            if (p->val < val){
                int ret = find_next_thread_le(p->children[1-from], val);
                if (ret >= 0){
                    return ret;
                }
            }
            from = p->seq;
        }
        return -1;
    }
    // uint64_t min_persisted(){
    //     return root->val.load();
    // }
    uint64_t next_epoch_to_persist(int tid){
        return leaves[tid].val.load();
    }
};

#endif