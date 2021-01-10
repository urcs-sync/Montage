/**
 * Author:      Louis Jenkins & Benjamin Valpey
 * Date:        31 Mar 2020
 * Filename:    TGraph.hpp
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
#include <ratio>
#include <cstdlib>

// #pragma GCC optimize ("O0")

/**
 * SimpleGraph class.  Labels are of templated type K.
 */
template <size_t numVertices = 1024, size_t meanEdgesPerVertex=20, size_t vertexLoad = 50>
class TGraph : public RGraph{

    public:

        // We use smart pointers in the unordered_set, but we can only lookup by a key allocated
        // on the stack if and only if it is also wrapped into a smart pointer. We create a custom
        // 'deleter' function to control whether or not it will try to delete the wrapped pointer below
        // https://stackoverflow.com/a/17853770/4111188
        

        class Relation;

        struct RelationHash {
            std::size_t operator()(const Relation *r) const {
                return std::hash<int>()(r->src) ^ std::hash<int>()(r->dest);
            }
        };

        struct RelationEqual {
            bool operator()(const Relation *r1, const Relation *r2) const {
                return r1->src == r2->src && r1->dest == r2->dest;
            }
        };

        using Set = std::unordered_set<Relation*,RelationHash,RelationEqual>;

        class Vertex {
            public:
                Set adjacency_list;
                Set dest_list;
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

                bool operator==(const Relation *other) const {
                    return this->src == other->src && this->dest == other->dest;
                }
        };

        struct alignas(64) VertexMeta {
            Vertex* idxToVertex;// Transient set of transient vertices to index map
            std::mutex vertexLocks;// Transient locks for transient vertices
            uint32_t vertexSeqs;// Transient sequence numbers for transactional operations on vertices
        };

        // Allocates data structures and pre-loads the graph
        TGraph(GlobalTestConfig* gtc) {
            srand(time(NULL));
            size_t sz = numVertices;
            this->vMeta = new VertexMeta[numVertices];
            std::mt19937_64 gen(rand());
            std::uniform_int_distribution<> verticesRNG(0, numVertices - 1);
            std::uniform_int_distribution<> coinflipRNG(0, 100);
            std::cout << "Allocated core..." << std::endl;
            // Fill to vertexLoad
            for (int i = 0; i < numVertices; i++) {
                if (coinflipRNG(gen) <= vertexLoad) {
                    vMeta[i].idxToVertex = new Vertex(i,i);
                } else {
                    vMeta[i].idxToVertex = nullptr;
                }
                vMeta[i].vertexSeqs = 0;
            }

            std::cout << "Filled vertexLoad" << std::endl;

            // Fill to mean edges per vertex
            for (int i = 0; i < numVertices; i++) {
                if (vMeta[i].idxToVertex == nullptr) continue;
                for (int j = 0; j < meanEdgesPerVertex * 100 / vertexLoad; j++) {
                    int k = verticesRNG(gen);
                    while (k == i) {
                        k = verticesRNG(gen);
                    }
                    if (vMeta[k].idxToVertex != nullptr) {
                        Relation *in = new Relation(i, k, -1);
                        Relation *out = new Relation(i, k, -1);
                        auto ret = source(i).insert(in);
                        destination(k).insert(out);
                        if(ret.second==false){
                            // relation exists, reclaiming
                            delete in;
                            delete out;
                        }
                    }
                }
            }
            std::cout << "Filled mean edges per vertex" << std::endl;
        }

        // Obtain statistics of graph (|V|, |E|, average degree, vertex degrees)
        // Not concurrent safe...
        std::tuple<int, int, double, int *, int> grab_stats() {
            int numV = 0;
            int numE = 0;
            int *degrees = new int[numVertices];
            double averageEdgeDegree = 0;
            for (auto i = 0; i < numVertices; i++) {
                if (vMeta[i].idxToVertex != nullptr) {
                    numV++;
                    numE += source(i).size();
                    degrees[i] = source(i).size() + destination(i).size();
                } else {
                    degrees[i] = 0;
                }
            }
            averageEdgeDegree = numE / ((double) numV);
            return std::make_tuple(numV, numE, averageEdgeDegree, degrees, numVertices);
        }

        VertexMeta* vMeta;

        // Thread-safe and does not leak edges
        void clear() {
            for (auto i = 0; i < numVertices; i++) {
                lock(i);
            }
            for (auto i = 0; i < numVertices; i++) {
                std::vector<Relation*> toDelete(source(i).size() + destination(i).size());
                for (auto r : source(i)) toDelete.push_back(r);
                for (auto r : destination(i)) toDelete.push_back(r);
                source(i).clear();
                destination(i).clear();
                for (auto r : toDelete) delete r;
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
            // Note: We do not create a vertex if one is not found
            // also we do not add an edge even if it is found some of the time
            // to enable even constant load factor
            if (vMeta[src].idxToVertex == nullptr || vMeta[dest].idxToVertex == nullptr) {
                goto exitEarly;
            }
            if (has_relation(srcSet, &r)) {
                // Sanity check
                assert(has_relation(destSet, &r));
                goto exitEarly;
            }
            

            {
                Relation *out = new Relation(src, dest, weight);
                Relation *in = new Relation(src, dest, weight);
                srcSet.insert(out);
                destSet.insert(in);
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
            if (vMeta[src].idxToVertex == nullptr) {
                unlock(src);
                return false;
            }
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
                lock(src);
            } else {
                lock(src);
                lock(dest);
            }
            
            if (vMeta[src].idxToVertex != nullptr && vMeta[dest].idxToVertex != nullptr) {
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

        bool add_vertex(int vid) {
            std::mt19937_64 vertexGen(time(NULL));
            std::uniform_int_distribution<> uniformVertex(0,numVertices);
            bool retval = true;
            // Randomly sample vertices...
            std::vector<int> vec(meanEdgesPerVertex);
            for (size_t i = 0; i < meanEdgesPerVertex * 100 / vertexLoad; i++) {
                int u = uniformVertex(vertexGen);
                while (u == i) {
                    u = uniformVertex(vertexGen);
                }
                vec.push_back(u);
            }
            vec.push_back(vid);
            std::sort(vec.begin(), vec.end()); 
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());

            for (int u : vec) {
                lock(u);
            }

            if (vMeta[vid].idxToVertex == nullptr) {
                vMeta[vid].idxToVertex = new Vertex(vid, vid);
                for (int u : vec) {
                    if (vMeta[u].idxToVertex == nullptr) continue;
                    if (u == vid) continue;
                    Relation *in = new Relation(vid, u, -1);
                    Relation *out = new Relation(vid, u, -1);
                    source(vid).insert(in);
                    destination(u).insert(out);
                }
            } else {
                retval = false;
            }

            for (auto u = vec.rbegin(); u != vec.rend(); u++) {
                if (vMeta[vid].idxToVertex != nullptr && vMeta[*u].idxToVertex != nullptr) inc_seq(*u);
                unlock(*u);
            }
            return retval;
        }

        bool remove_vertex(int vid) {
startOver:
            {
                // Step 1: Acquire vertex and collect neighbors...
                std::vector<int> vertices;
                lock(vid);
                if (vMeta[vid].idxToVertex == nullptr) {
                    unlock(vid);
                    return false;
                }
                uint32_t seq = get_seq(vid);
                for (auto r : source(vid)) {
                    vertices.push_back(r->dest);
                }
                for (auto r : destination(vid)) {
                    vertices.push_back(r->src);
                }
                
                vertices.push_back(vid);
                std::sort(vertices.begin(), vertices.end()); 
                vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());

                // Step 2: Release lock, then acquire lock-order...
                unlock(vid);
                for (int _vid : vertices) {
                    lock(_vid);
                    if (vMeta[_vid].idxToVertex == nullptr && get_seq(vid) == seq) {
                        for (auto r : source(vid)) {
                            if (r->dest == _vid)
                            std::cout << "(" << r->src << "," << r->dest << ")" << std::endl;
                        }
                        for (auto r : destination(vid)) {
                            if (r->src == _vid)
                            std::cout << "(" << r->src << "," << r->dest << ")" << std::endl;
                        }
                        std::abort();
                    }
                }

                // Has vertex been changed? Start over
                if (get_seq(vid) != seq) {
                    for (auto _vid = vertices.rbegin(); _vid != vertices.rend(); _vid++) {
                        unlock(*_vid);
                    }
                    goto startOver;
                }

                // Has not changed, continue...
                // Step 3: Remove edges from all other
                // vertices that relate to this vertex
                for (int other : vertices) {
                    if (other == vid) continue;

                    Relation src(other, vid, -1);
                    Relation dest(vid, other, -1);
                    if (!has_relation(source(other), &src) || !has_relation(destination(other), &dest)) {
                        std::cout << "Observed pair (" << vid << "," << other << ") that was originally there but no longer is..." << std::endl;
                        for (auto r : source(vid)) {
                            if (r->dest == other)
                            std::cout << "Us: (" << r->src << "," << r->dest << ")" << std::endl;
                        }
                        for (auto r : destination(other)) {
                            if (r->src == vid) {
                                std::cout << "Them: (" << r->src << "," << r->dest << ")" << std::endl;
                            }
                        }
                        for (auto r : destination(vid)) {
                            if (r->src == other) {
                                std::cout << "Us: (" << r->src << "," << r->dest << ")" << std::endl;
                            }
                        }
                        for (auto r : source(other)) {
                            if (r->dest == vid) {
                                std::cout << "Them: (" << r->src << "," << r->dest << ")" << std::endl;
                            }
                        }
                        std::abort();
                    }
                    remove_relation(source(other), &src);
                    remove_relation(destination(other), &dest);
                    assert(!has_relation(source(other), &src) && !has_relation(destination(other), &dest));
                }                
                
                std::vector<Relation*> toDelete(source(vid).size() + destination(vid).size());
                for (auto r : source(vid)) toDelete.push_back(r);
                for (auto r : destination(vid)) toDelete.push_back(r);
                source(vid).clear();
                destination(vid).clear();
                destroy(vid);
                for (auto r : toDelete) delete r;
                
                // Step 4: Release in reverse order
                for (auto _vid = vertices.rbegin(); _vid != vertices.rend(); _vid++) {
                    inc_seq(*_vid);
                    unlock(*_vid);
                }
            }
            return true;
        }
        
    private:
        void lock(size_t idx) {
        	vMeta[idx].vertexLocks.lock();
	}

        void unlock(size_t idx) {
        	vMeta[idx].vertexLocks.unlock();
	}

        // Lock must be owned for next operations...
        void inc_seq(size_t idx) {
            vMeta[idx].vertexSeqs++;
        }
            
        uint64_t get_seq(size_t idx) {
            return vMeta[idx].vertexSeqs;
        }

        void destroy(size_t idx) {
            assert(vMeta[idx].idxToVertex!=nullptr);
            delete vMeta[idx].idxToVertex;
            vMeta[idx].idxToVertex = nullptr;
        }

        // Incoming edges
        Set& source(int idx) {
            return vMeta[idx].idxToVertex->adjacency_list;

        }

        // Outgoing edges
        Set& destination(int idx) {
            return vMeta[idx].idxToVertex->dest_list;
        }

        bool has_relation(Set& set, Relation *r) {
            auto search = set.find(r);
            return search != set.end();
        }

        bool remove_relation(Set& set, Relation *r) {
            auto search = set.find(r);
            if (search != set.end()) {
                Relation *tmp = *search;
                set.erase(search);
                delete tmp;
                return true;
            }
            return false;
        }
};

template <size_t numVertices = 1024, size_t meanEdgesPerVertex=20, size_t vertexLoad = 50>
class TGraphFactory : public RideableFactory{
    Rideable *build(GlobalTestConfig *gtc){
        return new TGraph<numVertices, meanEdgesPerVertex, vertexLoad>(gtc);
    }
};
// #pragma GCC reset_options

#endif

