/* Author: Louis Jenkins
 *
 * Measures performance of using Ralloc but without Montage
 */

#ifndef NVMGRAPH_HPP
#define NVMGRAPH_HPP


#include "TestConfig.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RGraph.hpp"
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include "RCUTracker.hpp"
#include "Recoverable.hpp"

/**
 * SimpleGraph class.  Labels are of templated type K.
 */
template <size_t numVertices = 1024>
class NVMGraph : public RGraph {

    public:

        class tVertex;

        class Vertex : public Persistent {
            int id;
            int lbl;
            public:
            Vertex(){}
            Vertex(int id, int lbl): id(id), lbl(lbl){}
            Vertex(const Vertex& oth): id(oth.id), lbl(oth.lbl) {}
            bool operator==(const Vertex& oth) const { return id==oth.id;}
            void set_lbl(int lbl) { this->lbl = lbl; }
            int get_lbl() { return this->lbl; }
            void set_id(int id) { this->id = id; }
            int get_id() { return this->id; }
            void persist();
        };

        class Relation : public Persistent {
            int weight;
            int src;
            int dest;
            public:
            Relation(){}
            Relation(Vertex* src, Vertex* dest, int weight): weight(weight), src(src->id), dest(dest->id){}
            Relation(tVertex *src, tVertex *dest, int weight): weight(weight), src(src->get_id()), dest(dest->get_id()){}
            Relation(const Relation& oth): weight(oth.weight), src(oth.src), dest(oth.dest){}
            void set_weight(int weight) { this->weight = weight; }
            int get_weight() { return this->weight; }
            int get_src() { return this->src; }
            int get_dest() { return this->dest; }
            void persist(){}
        };

        class tVertex {
            public:
                Vertex *payload;

                int id; // cached id
                uint64_t seqNumber;

                std::unordered_set<Relation*> adjacency_list;

                std::unordered_set<Relation*> dest_list;

                std::mutex lck;

                tVertex(int id, int lbl) {
                    payload = new Vertex(id, lbl);
                    this->id = id;
                }

                ~tVertex() { delete payload; }

                void set_lbl(int l) {
                    payload->set_lbl(l);
                }
                int get_lbl() {
                    return payload->get_lbl();
                }
                int get_id() {
                    return id;
                }

                Vertex *payload_ptr() {
                    return payload;
                }
                
                void lock() {
                    lck.lock();
                }

                void unlock() {
                    lck.unlock();
                }
        };

        NVMGraph(GlobalTestConfig* gtc) {
            Persistent::init();
            idxToVertex = new tVertex*[numVertices];
            // Initialize...
            for (size_t i = 0; i < numVertices; i++) {
                idxToVertex[i] = new tVertex(i, -1);
            }
        }

        ~NVMGraph(){
            Persistent::finalize();
        }

        void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
            Persistent::init_thread(gtc, ltc);
        }

        tVertex** idxToVertex; // Transient set of transient vertices to index map

        // Thread-safe and does not leak edges
        void clear() {
            // BEGIN_OP_AUTOEND();
            for (size_t i = 0; i < numVertices; i++) {
                idxToVertex[i]->lock();
            }
            for (size_t i = 0; i < numVertices; i++) {
                for (Relation *r : idxToVertex[i]->adjacency_list) {
                    delete r;
                }
                idxToVertex[i]->adjacency_list.clear();
                idxToVertex[i]->dest_list.clear();
            }
            for (size_t i = numVertices - 1; i >= 0; i--) {
                idxToVertex[i]->seqNumber++;
                idxToVertex[i]->unlock();
            }
        }

        /**
         * Adds an edge to the graph, given two node IDs
         * @param src A pointer to the source node
         * @param dest A pointer to the destination node
         * @return Whether or not adding the edge was successful
         */
        bool add_edge(int src, int dest, int weight) {
            if (src == dest) return false; // Loops not allowed
            tVertex *v1 = idxToVertex[src];
            tVertex *v2 = idxToVertex[dest];
            if (src > dest) {
                v2->lock();
                v1->lock();
            } else {
                v1->lock();
                v2->lock();
            }
            
            {
                Relation* r = new Relation(v1, v2, weight);
                v1->adjacency_list.insert(r);
                v2->dest_list.insert(r);
            }
            
            v1->seqNumber++;
            v2->seqNumber++;
            if (src > dest) {
                v1->unlock();
                v2->unlock();
            } else {
                v2->unlock();
                v1->unlock();
            }
            return true;
        }

        bool has_edge(int v1, int v2) {
            bool retval = false;
            tVertex *v = idxToVertex[v1];
            
            // We utilize `get_unsafe` API because the Relation destination and vertex id will not change at all.
            v->lock();            
            {
                if (std::any_of(v->adjacency_list.begin(), v->adjacency_list.end(), 
                            [=] (Relation *r) { return r->get_dest() == v2; })) {
                    retval = true;
                }
            }
            v->unlock();
            
            return retval;
        }

        /**
         * Removes an edge from the graph. Acquires the unique_lock.
         * @param src The integer id of the source node of the edge.
         * @param dest The integer id of the destination node of the edge
         * @return True if the edge exists
         */
        bool remove_edge(int src, int dest) {
            if (src == dest) return false;
            tVertex *v1 = idxToVertex[src];
            tVertex *v2 = idxToVertex[dest];
            if (src > dest) {
                v2->lock();
                v1->lock();
            } else {
                v1->lock();
                v2->lock();
            }
            
            {
                // Scan v1 for an edge containing v2 in its adjacency list...
                Relation *rdel = nullptr;
                for (Relation *r : v1->adjacency_list) {
                    if (r->get_dest() == v2->get_id()) {
                        rdel = r;
                        v1->adjacency_list.erase(r);
                        break;
                    }
                }
            
                if (rdel){
                    v2->dest_list.erase(rdel);
                    delete rdel;
                }
            }
            
            v1->seqNumber++;
            v2->seqNumber++;
            if (src > dest) {
                v1->unlock();
                v2->unlock();
            } else {
                v2->unlock();
                v1->unlock();
            }
            return true;
        }

        /**
         * Sets the label for a node to a specific value
         * @param id The id the node whose weight to set
         * @param l The new label for the node
         */
        bool set_lbl(int id, int l) {
            tVertex *v = idxToVertex[id]; 
            v->lock();
            v->set_lbl(l);
            v->unlock();
            return true;
        }

        /**
         * Sets the weight for an edge to a specific value. If the edge does not exist, this does not break, but does
         * unnecessary computation.
         * @param src the integer id of the source of the edge to set the weight for
         * @param dest the integer id of the dest of the edge to set the weight for
         * @param w the new weight value
         */
        bool set_weight(int src, int dest, int w) {
            bool retval = false;
            // Unimplemented because MontageGraph can't 
            return retval;
        }

        bool clear_vertex(int id) {
startOver:
            {
                // Step 1: Acquire vertex and collect neighbors...
                std::vector<int> vertices;
                tVertex *v = idxToVertex[id];
                v->lock();
                uint64_t seq = v->seqNumber;
                for (Relation *r : v->adjacency_list) {
                    vertices.push_back(r->get_dest());
                }
                for (Relation *r : v->dest_list) {
                    vertices.push_back(r->get_src());
                }
                
                vertices.push_back(id);
                std::sort(vertices.begin(), vertices.end()); 
                vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());

                // Step 2: Release lock, then acquire lock-order...
                v->unlock();
                for (int _vid : vertices) {
                    idxToVertex[_vid]->lock();
                }

                // Has vertex been changed? Start over
                if (v->seqNumber != seq) {
                    std::reverse(vertices.begin(), vertices.end());
                    for (int _vid : vertices) {
                        idxToVertex[_vid]->unlock();
                    }
                    goto startOver;
                }

                // Has not changed, continue...
                // Step 3: Remove edges from all other
                // vertices that relate to this vertex
                std::vector<Relation*> garbageList;
                for (int _vid : vertices) {
                    if (_vid == id) continue;
                    tVertex *_v = idxToVertex[_vid];
                    std::vector<Relation*> toRemoveList;

                    for (Relation *r : _v->adjacency_list) {
                        if (r->get_src() == id) {
                            toRemoveList.push_back(r);
                        }
                    }
                    for (Relation *r : toRemoveList) {
                        _v->adjacency_list.erase(r);
                        garbageList.push_back(r);
                    }
                    toRemoveList.clear();

                    for (Relation *r : _v->dest_list) {
                        if (r->get_dest() == id) {
                            toRemoveList.push_back(r);
                        }
                    }
                    for (Relation *r : toRemoveList) {
                        _v->dest_list.erase(r);
                        garbageList.push_back(r);
                    }
                }
                
                // Step 4: Delete edges, clear set of src and dest edges
                v->adjacency_list.clear();
                v->dest_list.clear();
                for (Relation *r : garbageList) {
                    delete r;
                }
                
                // Step 5: Release in reverse order
                std::reverse(vertices.begin(), vertices.end());
                for (int _vid : vertices) {
                    idxToVertex[_vid]->seqNumber++;
                    idxToVertex[_vid]->unlock();
                }
            }
            return true;
        }
        
        void for_each_edge(int v, std::function<bool(int)> fn) {
            idxToVertex[v]->lock();
            for (Relation *r : idxToVertex[v]->adjacency_list) {
                if (!fn(r->get_dest())) {
                    break;
                }
            }
            idxToVertex[v]->unlock();
        }
};

template <size_t numVertices = 1024>
class NVMGraphFactory : public RideableFactory{
    Rideable *build(GlobalTestConfig *gtc){
        return new NVMGraph<numVertices>(gtc);
    }
};
#endif
