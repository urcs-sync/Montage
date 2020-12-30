/**
 * Author:      Louis Jenkins & Benjamin Valpey
 * Date:        31 Mar 2020
 * Filename:    PGraph.hpp
 * Description: A simple implementation of a Transient Graph
 */

#ifndef TGRAPH_HPP
#define TGRAPH_HPP

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

/**
 * SimpleGraph class.  Labels are of templated type K.
 */
template <size_t numVertices = 1024>
class TGraph : public RGraph{

    public:
        class Relation;
        class Vertex {
            public:
                std::unordered_set<Relation*> adjacency_list;
                std::unordered_set<Relation*> dest_list;
                int id;
                int lbl;
                std::mutex lck;
                uint64_t seqNumber; // Keeps track of number of changes made
                Vertex(int id, int lbl): id(id), lbl(lbl){}
                Vertex(const Vertex& oth): id(oth.id), lbl(oth.lbl){}
                bool operator==(const Vertex& oth) const { return id==oth.id;}
                void set_lbl(int l) {
                    lbl = l;
                }
                int get_lbl() {
                    return lbl;
                }
                int get_id() {
                    return id;
                }


                void lock() {
                    lck.lock();
                }

                void unlock() {
                    lck.unlock();
                }

        };

        class Relation {
            public:
                int src;
                int dest;
                int weight;
                Relation(){}
                Relation(int src, int dest, int weight): src(src), dest(dest), weight(weight){}
                Relation(Vertex* src, Vertex* dest, int weight): src(src->get_id()), dest(dest->get_id()), weight(weight){}
                Relation(const Relation& oth): src(oth.src), dest(oth.dest), weight(oth.weight){}
                void set_weight(int w) {
                    weight = w;
                }
                int get_weight() {
                    return weight;
                }
        };

        // Allocates data structures and pre-loads the graph
        TGraph(GlobalTestConfig* gtc) {
            idxToVertex = new Vertex*[numVertices];
            vertexLocks = new std::atomic<bool>[numVertices];
            vertexSeqs = new uint32_t[numVertices];

            // Initialize...
            for (size_t i = 0; i < numVertices; i++) {
                idxToVertex[i] = new Vertex(i, -1);
            }
        }

        Vertex** idxToVertex; // Transient set of transient vertices to index map
        std::atomic<bool> *vertexLocks; // Transient locks for transient vertices
        uint32_t *vertexSeqs; // Transient sequence numbers for transactional operations on vertices

        // Thread-safe and does not leak edges
        void clear() {
            for (int i = 0; i < numVertices; i++) {
                lock(i);
            }
            for (int i = 0; i < numVertices; i++) {
                for (Relation *r : idxToVertex[i]->adjacency_list) {
                    delete r;
                }
                source(i).clear();
                destination(i).clear();
            }
            for (int i = numVertices - 1; i >= 0; i--) {
                destroy(i);
                inc_seq(i);
                unlock(i);
            }
        }

        bool add_edge(int src, int dest, int weight) {
            bool retval = false;
            if (src == dest) return false; // Loops not allowed
            if (src > dest) {
                lock(dest);
                lock(src);
            } else {
                lock(src);
                lock(dest);
            }
            
            Relation r(src,dest,weight);
            auto& srcSet = source(src);
            auto& destSet = destination(dest);
            if (has_relation(srcSet, &r)) {
                // Sanity check
                assert(has_relation(destSet, &r));
                goto exitEarly;
            }

            {
                Relation *rel = new Relation(src, dest, weight);
                srcSet.insert(rel);
                destSet.insert(rel);
                inc_seq(src);
                inc_seq(dest);
                retval = true;
            }

            exitEarly:
                if (src > dest) {
                    unlock(src);
                    unlock(dest);
                } else {
                    unlock(dest);
                    unlock(src);
                }
                return retval;
        }


        bool has_edge(int src, int dest) {
            bool retval = false;
            
            // We utilize `get_unsafe` API because the Relation destination and vertex id will not change at all.
            lock(src);
            Relation r(src, dest, -1);
            retval = has_relation(source(src), &r);
            unlock(src);

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
            if (src > dest) {
                lock(dest);
                lock(src)
            } else {
                lock(src);
                lock(dest);
            }
            
            {
                Relation r(src, dest, -1);
                remove_relation(source(src), &r);
                remove_relation(destination(dest), &r);
                inc_seq(src);
                inc_seq(dest);
            }

            if (src > dest) {
                unlock(src);
                unlock(dest);
            } else {
                unlock(dest);
                unlock(src);
            }
            return true;
        }

        /**
         * Sets the label for a node to a specific value
         * @param id The id the node whose weight to set
         * @param l The new label for the node
         */
        bool set_lbl(int id, int l) {
            Vertex *v = idxToVertex[id]; 
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
                Vertex *v = idxToVertex[id];
                v->lock();
                uint64_t seq = v->seqNumber;
                for (Relation *r : v->adjacency_list) {
                    vertices.push_back(r->dest);
                }
                for (Relation *r : v->dest_list) {
                    vertices.push_back(r->src);
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
                    Vertex *_v = idxToVertex[_vid];
                    std::vector<Relation*> toRemoveList;

                    for (Relation *r : _v->adjacency_list) {
                        if (r->src == id) {
                            toRemoveList.push_back(r);
                        }
                    }
                    for (Relation *r : toRemoveList) {
                        _v->adjacency_list.erase(r);
                        garbageList.push_back(r);
                    }
                    toRemoveList.clear();

                    for (Relation *r : _v->dest_list) {
                        if (r->dest == id) {
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
                if (!fn(r->dest)) {
                    break;
                }
            }
            idxToVertex[v]->unlock();
        }
        
    private:
        void lock(size_t idx) {
            std::atomic<bool>& lck = vertexLocks[idx];
            bool expect = false;
            while (lck.load() == true || lck.compare_exchange_strong(expect, true) == false) {
                expect = false;
            }
        }

        void unlock(size_t idx) {
            std::atomic<bool>& lck = vertexLocks[idx];
            lck.store(false);
        }

        // Lock must be owned for next operations...
        void inc_seq(size_t idx) {
            vertexSeqs[idx]++;
        }
            
        uint64_t get_seq(size_t idx) {
            return vertexSeqs[idx];
        }

        void destroy(size_t idx) {
            delete idxToVertex[idx];
            idxToVertex[idx] = nullptr;
        }

        // Incoming edges
        std::unordered_set<Relation*>& source(size_t idx) {
            return idxToVertex[idx]->adjacency_list;
        }

        // Outgoing edges
        std::unordered_set<Relation*>& destination(size_t idx) {
            return idxToVertex[idx]->dest_list;
        }

        bool has_relation(std::unordered_set<Relation*>& set, Relation *r) {
            auto search = set.find(r);
            return search != set.end();
        }

        void remove_relation(std::unordered_set<Relation*>& set, Relation *r) {
            auto search = set.find(r);
            if (search != set.end()) {
                set.erase(search);
            }
        }
};

template <size_t numVertices = 1024>
class TGraphFactory : public RideableFactory{
    Rideable *build(GlobalTestConfig *gtc){
        return new TGraph<numVertices>(gtc);
    }
};

#endif
