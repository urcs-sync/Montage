#ifndef UNBALANCED_TREE_HPP
#define UNBALANCED_TREE_HPP

#include "TestConfig.hpp"
#include "RMap.hpp"
#include "Recoverable.hpp"
#include "CustomTypes.hpp"
#include <mutex>
#include <shared_mutex>

using namespace pds;

template<typename K, typename V>
class UnbalancedTree : public RMap<K,V>, public Recoverable{
    const optional<V> NONE = {}; // to prevent compiler warnings. TODO: switch to std::optional<>.
public:
    class Payload : public PBlk{
        GENERATE_FIELD(K, key, Payload);
        GENERATE_FIELD(V, val, Payload);
        GENERATE_FIELD(int, deleted, Payload);
    public:
        Payload(){}
        Payload(K x, V y): m_key(x), m_val(y), m_deleted(false){}
        // Payload(const Payload& oth): PBlk(oth), m_key(oth.m_key), m_val(oth.m_val), m_deleted(oth.m_deleted){}
        void persist(){}
    };

    struct TreeNode{
        UnbalancedTree* ds;
        // Transient-to-persistent pointer
        Payload* payload = nullptr;
        // Transient-to-transient pointers
        TreeNode* left = nullptr;
        TreeNode* right = nullptr;
        
        std::mutex lock;

        TreeNode(UnbalancedTree* ds_, K key, V val): ds(ds_){
            payload = ds->pnew<Payload>(key, val);
        }
        K get_key(){
            assert(payload!=nullptr && "payload shouldn't be null");
            return (K)payload->get_key(ds);
        }
        V get_val(){
            assert(payload!=nullptr && "payload shouldn't be null");
            return (V)payload->get_val(ds);
        }
        int get_deleted(){
            assert(payload!=nullptr && "payload shouldn't be null");
            return (int)payload->get_deleted(ds);
        }
        void set_val(V v){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload = payload->set_val(ds, v);
        }
        void set_deleted(int d){
            assert(payload!=nullptr && "payload shouldn't be null");
            payload = payload->set_deleted(ds, d);
        }
        ~TreeNode(){
            ds->pdelete(payload);
        }
    };

    TreeNode* root = nullptr;

    UnbalancedTree(GlobalTestConfig* gtc): Recoverable(gtc){
        root = nullptr;
    }

    int recover(bool simulated){
        errexit("recover of UnbalancedTree not implemented");
        return 0;
    }


    optional<V> get(K key, int tid){
        while(true){
            MontageOpHolder(this);
            if (!root){
                return NONE;
            } else {
                try{
                    HOHLockHolder lock_holder;
                    return do_get(&lock_holder, root, key);
                } catch(OldSeeNewException& e){
                    continue;
                }
            }
        }
    }

    optional<V> do_get(HOHLockHolder* lock_holder, TreeNode* curr, K key){
        // may throw OldSeeNewException:
        lock_holder->hold(&curr->lock);
        K curr_key = curr->get_key();
        if (curr_key == key){
            if (curr->get_deleted()){
                return NONE;
            }
            optional<V> ret = curr->get_val();
            return ret;
        } else if (curr_key > key){
            if (curr->left){
                return do_get(lock_holder, curr->left, key);
            } else {
                return NONE;
            }
        } else {
            if (curr->right){
                return do_get(lock_holder, curr->right, key);
            } else {
                return NONE;
            }
        }
    }

    optional<V> put(K key, V val, int tid){
        while(true){
            MontageOpHolder(this);
            if (!root){
                root = new TreeNode(key, val);
            } else {
                try{
                    HOHLockHolder lock_holder;
                    return do_put(&lock_holder, root, key, val);
                } catch (OldSeeNewException& e){
                    continue;
                }
            }
        }
    }

    optional<V> do_put(HOHLockHolder* lock_holder, TreeNode* curr, K key, V val){
        // may throw OldSeeNewException:
        lock_holder->hold(&curr->lock);
        K curr_key = curr->get_key();
        if (curr_key == key){
            optional<V> ret = curr->get_val();
            curr->payload = curr->payload->set_val(val);
            if (curr->get_deleted()){
                curr->payload = curr->payload->set_deleted(false);
                return NONE;
            } else {
                return ret;
            }
        } else if (curr_key > key){
            if (curr->left){
                return do_put(lock_holder, curr->left, key, val);
            } else {
                curr->left = new TreeNode(key, val);
                return NONE;
            }
        } else {
            if (curr->right){
                return do_put(lock_holder, curr->right, key, val);
            } else {
                curr->right = new TreeNode(key, val);
                return NONE;
            }
        }
    }

    bool insert(K key, V val, int tid){
        while(true){
            MontageOpHolder(this);
            if (!root){
                root = new TreeNode(key, val);
                return true;
            } else {
                try{
                    HOHLockHolder lock_holder;
                    return do_insert(&lock_holder, root, key, val);
                } catch (OldSeeNewException& e){
                    continue;
                }
            }
        }
    }

    bool do_insert(HOHLockHolder* lock_holder, TreeNode* curr, K key, V val){
        lock_holder->hold(&curr->lock);
        K curr_key = curr->get_key();
        if (curr_key == key){
            if (curr->get_deleted()){
                curr->payload = curr->payload->set_deleted(false);
                curr->payload = curr->payload->set_val(val);
                return true;
            } else {
                return false;
            }
        } else if (curr_key > key){
            if (curr->left){
                return do_insert(lock_holder, curr->left, key, val);
            } else {
                curr->left = new TreeNode(key, val);
                return true;
            }
        } else {
            if (curr->right){
                return do_insert(lock_holder, curr->right, key, val);
            } else {
                curr->right = new TreeNode(key, val);
                return true;
            }
        }
    }

    optional<V> replace(K key, V val, int tid){
        assert(false && "replace not implemented yet.");
        return NONE;
    }

    optional<V> remove(K key, int tid){
        while(true){
            MontageOpHolder(this);
            if (!root){
                return NONE;
            } else {
                try{
                    HOHLockHolder lock_holder;
                    return do_remove(&lock_holder, root, key);
                } catch (OldSeeNewException& e){
                    continue;
                }
            }
        }
    }

    optional<V> do_remove(HOHLockHolder* lock_holder, TreeNode*& curr, K key){
        lock_holder->hold(&curr->lock);
        K curr_key = curr->get_key();
        if (curr_key == key){
            if (curr->get_deleted()){
                return NONE;
            } else {
                curr->payload = curr->payload->set_deleted(true);
                return curr->get_val();
            }
        } else if (curr_key > key){
            if (curr->left){
                return do_remove(lock_holder, curr->left, key);
            } else {
                return NONE;
            }
        } else {
            if (curr->right){
                return do_remove(lock_holder, curr->right, key);
            } else {
                return NONE;
            }
        }
    }

    // optional<V> do_remove(TreeNode*& curr, K key){
    //     // may throw OldSeeNewException:
    //     K curr_key = curr->get_key();
    //     if (curr_key == key){
    //         optional<V> ret = curr->get_val();
    //         TreeNode* sub = nullptr;
    //         if (!curr->left && !curr->right){
    //             delete curr;
    //             curr = nullptr;
    //             return ret;
    //         }
    //         if (curr->left){
    //             sub = pop_rightmost(curr->left);
    //         } else {//if (curr->right)
    //             sub = pop_leftmost(curr->right);
    //         }
    //         swap_payload(curr, sub);
    //         delete sub;
    //         return ret;
    //     } else if (curr_key > key){
    //         if (curr->left){
    //             return do_remove(curr->left, key);
    //         } else {
    //             return NONE;
    //         }
    //     } else {
    //         if (curr->right){
    //             return do_remove(curr->right, key);
    //         } else {
    //             return NONE;
    //         }
    //     }
    // }

    // TreeNode* pop_rightmost(TreeNode*& curr){
    //     if (curr->right){
    //         return pop_rightmost(curr->right);
    //     } else {
    //         TreeNode* ret = curr;
    //         curr = curr->left;
    //         return ret;
    //     }
    // }

    // TreeNode* pop_leftmost(TreeNode*& curr){
    //     if (curr->left){
    //         return pop_leftmost(curr->left);
    //     } else {
    //         TreeNode* ret = curr;
    //         curr = curr->right;
    //         return ret;
    //     }
    // }

    // void swap_payload(TreeNode* x, TreeNode* y){
    //     Payload* tmp = x->payload;
    //     x->payload = y->payload;
    //     y->payload = tmp;
    // }
};

template <class T> 
class UnbalancedTreeFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new UnbalancedTree<T, T>(gtc);
    }
};


/* Specialization for strings */
#include <string>
#include "PString.hpp"
template <>
class UnbalancedTree<std::string, std::string>::Payload : public PBlk{
    GENERATE_FIELD(PString<TESTS_KEY_SIZE>, key, Payload);
    GENERATE_FIELD(PString<TESTS_VAL_SIZE>, val, Payload);
    GENERATE_FIELD(int, deleted, Payload);

public:
    Payload(std::string k, std::string v) : m_key(this, k), m_val(this, v), m_deleted(false){}
    void persist(){}
};
#endif