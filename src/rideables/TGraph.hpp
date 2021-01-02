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
                std::unordered_set<std::shared_ptr<Relation>> adjacency_list;
                std::unordered_set<std::shared_ptr<Relation>> dest_list;
                int id;
                int lbl;
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
        
        bool remove_vertex(int vid) {
startOver:
            {
                // Step 1: Acquire vertex and collect neighbors...
                std::vector<int> vertices;
                lock(vid);
                uint32_t seq = get_seq(vid);
                for (Relation *r : source(vid)) {
                    vertices.push_back(r->dest);
                }
                for (Relation *r : destination(vid)) {
                    vertices.push_back(r->src);
                }
                
                vertices.push_back(vid);
                std::sort(vertices.begin(), vertices.end()); 
                vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());

                // Step 2: Release lock, then acquire lock-order...
                unlock(vid);
                for (int _vid : vertices) {
                    lock(_vid);
                }

                // Has vertex been changed? Start over
                if (get_seq(vid) != seq) {
                    std::reverse(vertices.begin(), vertices.end());
                    for (int _vid : vertices) {
                        unlock(_vid);
                    }
                    goto startOver;
                }

                // Has not changed, continue...
                // Step 3: Remove edges from all other
                // vertices that relate to this vertex
                for (int other : vertices) {
                    if (other == vid) continue;
                    std::vector<Relation*> toRemoveList;

                    Relation src(other, vid, -1);
                    Relation dst(vid, other, -1);
                    remove_relation(source(other), &src);
                    remove_relation(destination(other), &dst);

                    // Last relation, delete this vertex
                    if (source(other).size() == 0 && destination(other).size() == 0) {
                        destroy(other);
                    }
                }
                
                // Step 4: Delete edges, clear set of src and dest edges, then delete the vertex itself
                std::vector<Relation*> garbageList(source(vid).size() + destination(vid).size());
                garbageList.insert(garbageList.begin(), source(vid).begin(), source(vid).end());
                garbageList.insert(garbageList.begin(), destination(vid).begin(), destination(vid).end());
                source(vid).clear();
                destination(vid).clear();
                for (Relation *r : garbageList) {
                    delete r;
                }
                destroy(vid);
                
                // Step 5: Release in reverse order
                std::reverse(vertices.begin(), vertices.end());
                for (int _vid : vertices) {
                    inc_seq(_vid);
                    unlock(_vid);
                }
            }
            return true;
        }
        
        void for_each_outgoing(int vid, std::function<bool(int)> fn) {
            lock(v);
            for (Relation *r : source(v)) {
                if (!fn(r->dest)) {
                    break;
                }
            }
            unlock(v);
        }

        void for_each_incoming(int vid, std::function<bool(int)> fn) {
            lock(v);
            for (Relation *r : destination(v)) {
                if (!fn(r->src)) {
                    break;
                }
            }
            unlock(v);
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
