/*   
 *   File: bst-aravind.h
 *   Author: Tudor David <tudor.david@epfl.ch>
 *   Description: Aravind Natarajan and Neeraj Mittal. 
 *   Fast Concurrent Lock-free Binary Search Trees. PPoPP 2014
 *   bst-aravind.h is part of ASCYLIB
 *
 * Copyright (c) 2014 Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>,
 * 	     	      Tudor David <tudor.david@epfl.ch>
 *	      	      Distributed Programming Lab (LPD), EPFL
 *
 * ASCYLIB is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#ifndef NVT_NATARAJAN_TREE_HPP
#define NVT_NATARAJAN_TREE_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "HarnessUtils.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RMap.hpp"
#include "RCUTracker.hpp"
#include "Persistent.hpp"
#include "NVTraverse/utils.hpp"
#include "NVTraverse/pmem_utils.hpp"
#include "NVTraverse/new_pmem_utils.hpp"

struct alignas(64) seek_record_traverse{
	node_t* ancestor_grandparent;
	node_t* ancestor_parent;
	node_t* grandparent;
	node_t* ancestor;
	node_t* successor;
	node_t* parent;
	node_t* leaf;
	uint8_t padding[32];
};

// extern __thread ssmem_allocator_t* alloc;
// extern __thread ssmem_allocator_t* allocW;
__thread seek_record_traverse* seek_record_tr = nullptr;

namespace tr = traversal_datastructure_transformation;
//RETRY_STATS_VARS;

// template <class T> 
class NVTNatarajanTree : public RMap<std::string, std::string> {
    RCUTracker tracker;
    public:
	NVTNatarajanTree(GlobalTestConfig* gtc): 
        tracker(gtc->task_num, 100, 1000, true){
        Persistent::init();
        root = initialize_tree();
    }

    ~NVTNatarajanTree(){
        Persistent::finalize();
    }

    inline bool is_inf(const std::string& s){
        return (s.substr(0, 3) == "INF");
    }
    inline bool val_less(const std::string& a, const std::string& b){
        if (!is_inf(a) && !is_inf(b)){
            return (a < b);
        } else {
            return (!is_inf(a) || a < b);
        }
    }
	
    node_t* initialize_tree() {
        if (!seek_record_tr){
            seek_record_tr = new seek_record_traverse();
        }
        node_t* r;
        node_t* s;
        node_t* inf0;
        node_t* inf1;
        node_t* inf2;
        r = create_node_initial(DUMMY_TID, "INF2",std::to_string(0),1);
        s = create_node_initial(DUMMY_TID, "INF1",std::to_string(0),1);
        inf0 = create_node_initial(DUMMY_TID, "INF0",std::to_string(0),1);
        inf1 = create_node_initial(DUMMY_TID, "INF1",std::to_string(0),1);
        inf2 = create_node_initial(DUMMY_TID, "INF2",std::to_string(0),1);

        asm volatile("" ::: "memory");
        r->left = s;
        r->right = inf2;
        s->right = inf1;
        s->left= inf0;
        asm volatile("" ::: "memory");

        return r;
    }

//============================================
	
	void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc) {
        Persistent::init_thread(ltc->tid);
	    // seek_record_tr = (seek_record_traverse*) malloc (sizeof(seek_record_traverse));
        seek_record_tr = new seek_record_traverse();
  	    assert(seek_record_tr != NULL);
	}
//===========================================
	
	node_t* create_node(const int tid, const std::string& k, const std::string& value, int initializing) {
        node_t* new_node;
        // #if GC == 1
        //         new_node = (node_t*) ssmem_alloc(alloc, sizeof(node_t));
        // #else
        // 	new_node = (node_t*) malloc(sizeof(node_t));
        // #endif
        new_node = new node_t();
        if (new_node == NULL) {
            perror("malloc in bst create node");
            exit(1);
        }
        new_node->left =  (node_t*) NULL;
        new_node->right = (node_t*) NULL;
        new_node->key = k;
        new_node->value = value;

        tr::flush_node(tid, new_node);
        return (node_t*) new_node;
	}

//===========================================

    node_t* create_node_initial(const int tid, const std::string& k, const std::string& value, int initializing) {
        node_t* new_node;
        // new_node = (node_t*) malloc(sizeof(node_t));
        new_node = new node_t();
        if (new_node == NULL) {
            perror("malloc in bst create node");
            exit(1);
        }
        new_node->left =  (node_t*) NULL;
        new_node->right = (node_t*) NULL;
        new_node->key = k;
        new_node->value = value;
        tr::flush_node(tid, new_node);
        return (node_t*) new_node;
    }

//===========================================

	seek_record_traverse* bst_seek(const int tid, const std::string& key, node_t* node_r) {
	    //PARSE_TRY();
	    seek_record_traverse seek_record_l;
    	node_t* node_s = ADDRESS(node_r->left); // root is immutable
	    seek_record_l.ancestor_grandparent = NULL; // not a shared variable
        seek_record_l.ancestor_parent = NULL; // not a shared variable
        seek_record_l.ancestor = node_r; // not a shared variable
        seek_record_l.successor = node_s;
        seek_record_l.parent = node_s;
        seek_record_l.leaf = ADDRESS(node_s->left);

        node_t* parent_field = (node_t*) seek_record_l.parent->left;
        node_t* current_field = (node_t*) seek_record_l.leaf->left;
        node_t* current = ADDRESS(current_field);

        while (current != NULL) {
            if (!GETTAG(parent_field)) {
                seek_record_l.ancestor_grandparent = seek_record_l.ancestor_parent;
                seek_record_l.ancestor_parent = seek_record_l.ancestor;
                seek_record_l.ancestor = seek_record_l.parent;
                seek_record_l.successor = seek_record_l.leaf;
            }
            seek_record_l.parent = seek_record_l.leaf;
            seek_record_l.leaf = current;

            parent_field = current_field;
            if (val_less(key, current->key)) {
                current_field= (node_t*) current->left;
            } else {
                current_field= (node_t*) current->right;
            }
            current=ADDRESS(current_field);
        }
	    seek_record_tr->ancestor_grandparent=seek_record_l.ancestor_grandparent;
        seek_record_tr->ancestor_parent=seek_record_l.ancestor_parent;
        seek_record_tr->ancestor=seek_record_l.ancestor;
        seek_record_tr->successor=seek_record_l.successor;
        seek_record_tr->parent=seek_record_l.parent;
        seek_record_tr->leaf=seek_record_l.leaf;
        return seek_record_tr;
	}
//=================================================
	seek_record_traverse* bst_seek_leaf(const int tid, const std::string& key){
  	    //PARSE_TRY();
	    seek_record_traverse seek_record_l;
        node_t* node_s = ADDRESS(root->left); // root is immutable
        seek_record_l.grandparent = NULL;
        seek_record_l.parent = node_s;
        seek_record_l.leaf = ADDRESS(node_s->left);

        node_t* current_field = (node_t*) seek_record_l.leaf->left;
        node_t* current = ADDRESS(current_field);

        while (current != NULL) {
            seek_record_l.grandparent = seek_record_l.parent;
            seek_record_l.parent = seek_record_l.leaf;
            seek_record_l.leaf = current;

            if (val_less(key, current->key)) {
                current_field= (node_t*) current->left;
            } else {
                current_field= (node_t*) current->right;
            }
            current=ADDRESS(current_field);
        }
        seek_record_tr->grandparent=seek_record_l.grandparent;
        seek_record_tr->parent=seek_record_l.parent;
        seek_record_tr->leaf=seek_record_l.leaf;
        return seek_record_tr;
	}


//=================================================
	/*
    	    frees all nodes in the interval [node_start, node_stop]
	*/
	void free_path(const int tid, const std::string& key, node_t* node_start, node_t* node_stop) {
        node_t* current = node_start;
        node_t* current_field = NULL;

   	    while (current != node_stop) {
        	if(current == NULL) return; // this is a safety check, current should never be NULL
                // ssmem_free(alloc, current);
            tracker.retire(current, tid);
            if (val_less(key, current->key)) {
                current_field= (node_t*) current->left;
            } else {
                current_field= (node_t*) current->right;
            }
            current=ADDRESS(current_field);
    	}
        // ssmem_free(alloc, current);
        tracker.retire(current, tid);
	}

//===================================================

	optional<std::string> get(std::string key, int tid) {
        tracker.start_op(tid);
   	    bst_seek_leaf(tid, key);
   	    // the above traversal returns seek_record->leaf
   	    // ensure_reachable(leaf)
   	    if(seek_record_tr->grandparent) tr::flush_node(tid, seek_record_tr->grandparent);
   	    tr::flush_node(tid, seek_record_tr->parent);
   	    // make_persistent
   	    tr::flush_node(tid, seek_record_tr->leaf);
   	    if (tr::im_read(tid, seek_record_tr->leaf->key) == key) {
            tr::end_operation(tid);
            tracker.end_op(tid);
            return tr::im_read(tid, seek_record_tr->leaf->value);
        } else {
            tr::end_operation(tid);
            tracker.end_op(tid);
            return {};
   	    }
	}
//===================================================
	/*
    	    flush_path performs the function of make_persistent in the paper.
    	    For this data structure, we are allowed to repeat part of the
    	    traversal for flushing because all the nodes between successor (node_start)
    	    and parent (node_stop) have been marked meaning that they cannot change.

    	    flush_path flushes all nodes in the interval [node_start, node_stop)
	*/
	void flush_path(const int tid, const std::string& key, node_t* node_start, node_t* node_stop) {
        node_t* current = node_start;
        node_t* current_field = NULL;

        while (current != node_stop) {
            if(current == NULL) return; // this is a safety check, current should never be NULL
            tr::flush_node(tid, current);
            if (val_less(key, current->key)) {
                current_field= (node_t*) current->left;
            } else {
                current_field= (node_t*) current->right;
            }
            current=ADDRESS(current_field);
        }
	}

//====================================================	
	bool insert(std::string key, std::string val, int tid) {
        node_t* new_internal = NULL;
        node_t* new_node = NULL;
        uint created = 0;
        tracker.start_op(tid);
        while (1) {
            //UPDATE_TRY();
            bst_seek(tid, key, root);

            // ensure_reachable(ancestor)
            if(seek_record_tr->ancestor_grandparent) tr::flush_node(tid, seek_record_tr->ancestor_grandparent);
            if(seek_record_tr->ancestor_parent) tr::flush_node(tid, seek_record_tr->ancestor_parent);

            // make_persistent
            tr::flush_node(tid, seek_record_tr->ancestor);
            flush_path(tid, key, seek_record_tr->successor, seek_record_tr->parent);
            tr::flush_node(tid, seek_record_tr->parent);
            tr::flush_node(tid, seek_record_tr->leaf);

            if (tr::im_read(tid, seek_record_tr->leaf->key) == key) {

                // #if GC == 1
                // if (created) {
                //     ssmem_free(alloc, new_internal);
                //     ssmem_free(alloc, new_node);
                // }
                // #endif
                if (created) {
                    tracker.retire(new_internal, tid);
                    tracker.retire(new_node, tid);
                }
                tr::end_operation(tid);
                tracker.end_op(tid);
                return FALSE;
            }

			node_t* parent = seek_record_tr->parent;
			node_t* leaf = seek_record_tr->leaf;

			node_t** child_addr;
			if (val_less(key, tr::im_read(tid, parent->key))) {
				child_addr= (node_t**) &(parent->left);
			} else {
				child_addr= (node_t**) &(parent->right);
			}
            auto leaf_key = tr::im_read(tid, leaf->key);
			if (/*likely*/(created==0)) {
				// new_internal=create_node(tid, max(key,tr::im_read(tid, leaf->key)),0,0);
				new_internal=create_node(
					tid, val_less(key,leaf_key)? (std::string)leaf_key : key,
					"0",0);
				new_node = create_node(tid, key,val,0);
				created=1;
            } else {
            	tr::write(tid, new_internal->key,
					val_less(key,leaf_key)? (std::string)leaf_key : key);
            }
            if ( val_less(key, leaf_key)) {
                tr::write(tid, new_internal->left, new_node);
                    tr::write(tid, new_internal->right, leaf); 
            } else {
                    tr::write(tid, new_internal->right, new_node);
                    tr::write(tid, new_internal->left, leaf);
            }	        
        #ifdef __tile__
                MEM_BARRIER;
            #endif
                node_t* result = tr::cr_casv(tid, child_addr, ADDRESS(leaf), ADDRESS(new_internal));
            if (result == ADDRESS(leaf)) {
                    tr::end_operation(tid);
                    tracker.end_op(tid);
                    return TRUE;
            }
            node_t* chld = tr::cr_read(tid, *child_addr); 
            if ( (ADDRESS(chld)==leaf) && (GETFLAG(chld) || GETTAG(chld)) ) {
                    bst_cleanup(tid, key); 
            }
        }    
        tr::end_operation(tid);
        tracker.end_op(tid);
	}

//=====================================================

	optional<std::string> remove(std::string key, const int tid) {
        bool_t injecting = TRUE;
        node_t* leaf=nullptr;
        std::string val;
        tracker.start_op(tid);
        while (1) {
            //UPDATE_TRY();
            
            bst_seek(tid, key, root);
                
            // ensure_reachable(ancestor)
            if(seek_record_tr->ancestor_grandparent) tr::flush_node(tid, seek_record_tr->ancestor_grandparent);
            if(seek_record_tr->ancestor_parent) tr::flush_node(tid, seek_record_tr->ancestor_parent);

            // make_persistent
            tr::flush_node(tid, seek_record_tr->ancestor);
            flush_path(tid, key, seek_record_tr->successor, seek_record_tr->parent);
            tr::flush_node(tid, seek_record_tr->parent);
            tr::flush_node(tid, seek_record_tr->leaf);

            val = tr::im_read(tid, seek_record_tr->leaf->value);
            node_t* parent = seek_record_tr->parent;

            node_t** child_addr;
            if (val_less(key, tr::im_read(tid, parent->key))) {
                child_addr = (node_t**) &(parent->left);
            } else {
                child_addr = (node_t**) &(parent->right);
            }

            if (injecting == TRUE) {
                leaf = seek_record_tr->leaf;
                if (tr::im_read(tid, leaf->key) != key) {
                    tr::end_operation(tid);
                    tracker.end_op(tid);
                    return {};
                }
                node_t* lf = ADDRESS(leaf);
                node_t* result = tr::cr_casv(tid, child_addr, lf, (node_t*) FLAG(lf));
                if (result == ADDRESS(leaf)) {
                    injecting = FALSE;
                    bool_t done = bst_cleanup(tid, key);
                    if (done == TRUE) {
                        tr::end_operation(tid);
                        tracker.end_op(tid);
                        return val;
                    }
                } else {
                    node_t* chld = tr::cr_read(tid, *child_addr);
                    if ( (ADDRESS(chld) == leaf) && (GETFLAG(chld) || GETTAG(chld)) ) {
                        bst_cleanup(tid, key);
                    }
                }
            } else {
                if (seek_record_tr->leaf != leaf) {
                    tr::end_operation(tid);
                    tracker.end_op(tid);
                    return val;
                } else {
                    bool_t done = bst_cleanup(tid, key);
                    if (done == TRUE) {
                        tr::end_operation(tid);
                        tracker.end_op(tid);
                        return val;
                    }
                }
            }
        }
        tr::end_operation(tid);
        tracker.end_op(tid);
		return {};
	}


//=====================================================

	bool_t bst_cleanup(const int tid, const std::string& key) {
	    node_t* ancestor = seek_record_tr->ancestor;
        node_t* successor = seek_record_tr->successor;
        node_t* parent = seek_record_tr->parent;

        node_t** succ_addr;
        if (val_less(key, tr::im_read(tid, ancestor->key))) {
            succ_addr = (node_t**) &(ancestor->left);
        } else {
            succ_addr = (node_t**) &(ancestor->right);
        }

        node_t** child_addr;
        node_t** sibling_addr;
        if (val_less(key, tr::im_read(tid, parent->key))) {
            child_addr = (node_t**) &(parent->left);
            sibling_addr = (node_t**) &(parent->right);
        } else {
            child_addr = (node_t**) &(parent->right);
            sibling_addr = (node_t**) &(parent->left);
        }

        node_t* chld = tr::cr_read(tid, *(child_addr));
        if (!GETFLAG(chld)) {
            chld = tr::cr_read(tid, *(sibling_addr));
            sibling_addr = child_addr;
        }
        while (1) {
            node_t* untagged = tr::cr_read(tid, *sibling_addr);
            node_t* tagged = (node_t*)TAG(untagged);
            node_t* res = tr::cr_casv(tid, sibling_addr, untagged, tagged);
            if (res == untagged) {
                break;
            }
        }

	    node_t* sibl = tr::cr_read(tid, *sibling_addr);
        if (tr::cr_casv(tid, succ_addr, ADDRESS(successor), (node_t*) UNTAG(sibl)) == (node_t*) ADDRESS(successor)) {
        // #if GC == 1
        //     ssmem_free(alloc, ADDRESS(chld));
        //     free_path(tid, parent->key, ADDRESS(successor), parent);
		// #endif
            tracker.retire(ADDRESS(chld), tid);
            free_path(tid, parent->key, ADDRESS(successor), parent);
        	return TRUE;
    	}
    	return FALSE;
	}

//=====================================================

	// uint32_t bst_size(volatile node_t* node) {
    //     if (node == NULL) return 0;
    //     if ((node->left == NULL) && (node->right == NULL)) {
    //         if (stoi(node->key) < INF0 ) return 1;
    //     }
    //     uint32_t l = 0;
    //     uint32_t r = 0;
    //     if ( !GETFLAG(node->left) && !GETTAG(node->left)) {
    //         l = bst_size(node->left);
    //     }
    //     if ( !GETFLAG(node->right) && !GETTAG(node->right)) {
    //         r = bst_size(node->right);
    //     }
    //     return l+r;
	// }

    optional<std::string> put(std::string key, std::string val, const int tid){
        errexit("NVTNatarajanTree::put not implemented.");
		return {};
    }

    optional<std::string> replace(std::string key, std::string val, const int tid){
        errexit("NVTNatarajanTree::replace not implemented.");
		return {};
    }

    private:
        node_t* root;
     
};

class NVTNatarajanTreeFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new NVTNatarajanTree(gtc);
    }
};


#endif
