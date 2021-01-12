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

// #pragma GCC optimize ("O0")

/**
 * SimpleGraph class.  Labels are of templated type K.
 */
template <size_t numVertices = 1024, size_t meanEdgesPerVertex=20, size_t vertexLoad = 50>
class NVMGraph : public RGraph {

    public:

        // We use smart pointers in the unordered_set, but we can only lookup by a key allocated
        // on the stack if and only if it is also wrapped into a smart pointer. We create a custom
        // 'deleter' function to control whether or not it will try to delete the wrapped pointer below
        // https://stackoverflow.com/a/17853770/4111188
        

        class Relation;

        struct RelationHash {
            std::size_t operator()(const pair<int,int>& r) const {
                return std::hash<int>()(r.first) ^ std::hash<int>()(r.second);
            }
        };

        struct RelationEqual {
            bool operator()(const pair<int,int>& r1, const pair<int,int>& r2) const {
                return r1.first == r2.first && r1.second == r2.second;
            }
        };

        using Map = std::unordered_map<pair<int,int>,Relation*,RelationHash,RelationEqual>;
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
            int get_lbl() const { return this->lbl; }
            void set_id(int id) { this->id = id; }
            int get_id() const { return this->id; }
            void persist();
        };
        class alignas(64) tVertex {
            public:
                Map adjacency_list;//only relations in this list is reclaimed
                Map dest_list;// relations in this list is a duplication of those in some adjacency list
                Vertex* payload;
                int id;
                int lbl;
                tVertex(int id, int lbl): id(id) {payload = new Vertex(id, lbl);}
                tVertex(const tVertex& oth): id(oth.id) {payload = new Vertex(oth.id, oth.payload->get_lbl());}
                bool operator==(const tVertex& oth) const { return payload->id==oth.payload->id;}
                void set_lbl(int l) {
                    payload->set_lbl(l);
                }
                int get_lbl() {
                    return payload->get_lbl();
                }
                int get_id() {
                    return id;
                }
        };

        class Relation : public Persistent {
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
                void persist(){}
        };

        struct alignas(64) VertexMeta {
            tVertex* idxToVertex = nullptr;// Transient set of transient vertices to index map
            std::mutex vertexLocks;// Transient locks for transient vertices
            uint32_t vertexSeqs = 0;// Transient sequence numbers for transactional operations on vertices
        };

        // Allocates data structures and pre-loads the graph
        NVMGraph(GlobalTestConfig* gtc) {
            Persistent::init();
            size_t sz = numVertices;
            this->vMeta = new VertexMeta[numVertices];
            std::mt19937_64 gen(time(NULL));
            std::uniform_int_distribution<> verticesRNG(0, numVertices - 1);
            std::uniform_int_distribution<> coinflipRNG(0, 100);
            if(gtc->verbose) std::cout << "Allocated core..." << std::endl;
            // Fill to vertexLoad
            for (int i = 0; i < numVertices; i++) {
                if (coinflipRNG(gen) <= vertexLoad) {
                    vertex(i) = new tVertex(i,i);
                } else {
                    vertex(i) = nullptr;
                }
                vMeta[i].vertexSeqs = 0;
            }

            if(gtc->verbose) std::cout << "Filled vertexLoad" << std::endl;

            // Fill to mean edges per vertex
            for (int i = 0; i < numVertices; i++) {
                if (vMeta[i].idxToVertex == nullptr) continue;
                for (int j = 0; j < meanEdgesPerVertex * 100 / vertexLoad; j++) {
                    int k = verticesRNG(gen);
                    if (k == i) {
                        continue;
                    }
                    if (vMeta[k].idxToVertex != nullptr) {
                        Relation *r = new Relation(i, k, -1);
                        auto p = make_pair(i,k);
                        auto ret1 = source(i).emplace(p,r);
                        auto ret2 = destination(k).emplace(p,r);
                        assert(ret1.second==ret2.second);
                        if(ret1.second==false){
                            // relation exists, reclaiming
                            delete r;
                        }
                    }
                }
            }
            if(gtc->verbose) std::cout << "Filled mean edges per vertex" << std::endl;
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
            assert(0&&"clear() not implemented!");
            // for (auto i = 0; i < numVertices; i++) {
            //     lock(i);
            // }
            // for (auto i = 0; i < numVertices; i++) {
            //     if (vertex(i) == nullptr) continue;
            //     std::vector<Relation*> toDelete(source(i).size() + destination(i).size());
            //     for (auto r : source(i)) toDelete.push_back(r);
            //     for (auto r : destination(i)) toDelete.push_back(r);
            //     source(i).clear();
            //     destination(i).clear();
            //     for (auto r : toDelete) delete r;
            // }
            // for (int i = numVertices - 1; i >= 0; i--) {
            //     destroy(i);
            //     inc_seq(i);
            //     unlock(i);
            // }
        }

        bool add_edge(int src, int dest, int weight) {
            bool retval = false;
            if (src == dest) return false; // Loops not allowed
            Relation *r = new Relation(src, dest, weight);
            auto p = make_pair(src,dest);
            if (src > dest) {
                lock(dest);
                lock(src);
            } else {
                lock(src);
                lock(dest);
            }  
            
            auto& srcSet = source(src);
            auto& destSet = destination(dest);
            // Note: We do not create a vertex if one is not found
            // also we do not add an edge even if it is found some of the time
            // to enable even constant load factor
            if (vertex(src) == nullptr || vertex(dest) == nullptr) {
                goto exitEarly;
            }
            if (has_relation(srcSet, p)) {
                // Sanity check
                assert(has_relation(destSet, p));
                goto exitEarly;
            }
            

            {
                auto ret1 = srcSet.emplace(p,r);
                auto ret2 = destSet.emplace(p,r);
                assert(ret1.second == ret2.second);
                if(ret1.second){
                    inc_seq(src);
                    inc_seq(dest);
                    retval = true;
                }else{
                    retval = false;
                }
            }

            exitEarly:
                if (!retval){
                    delete r;
                }
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
            if (vertex(src) == nullptr) {
                unlock(src);
                return false;
            }
            auto r = make_pair(src, dest);
            retval = has_relation(source(src), r);
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
            bool ret = false;
            if (vertex(src) != nullptr && vertex(dest) != nullptr) {
                auto r = make_pair(src, dest);
                auto ret1 = remove_relation(source(src), r);
                auto ret2 = remove_relation(destination(dest), r);
                assert(ret1==ret2);
                ret = (ret1!=nullptr);
                if(ret){
                    delete ret1;
                    inc_seq(src);
                    inc_seq(dest);
                }
            }

            if (src > dest) {
                unlock(src);
                unlock(dest);
            } else {
                unlock(dest);
                unlock(src);
            }
            return ret;
        }

        bool add_vertex(int vid) {
            std::mt19937_64 vertexGen(time(NULL));
            std::uniform_int_distribution<> uniformVertex(0,numVertices);
            bool retval = true;
            // Randomly sample vertices...
            std::vector<int> vec;
            for (size_t i = 0; i < meanEdgesPerVertex * 100 / vertexLoad; i++) {
                int u = uniformVertex(vertexGen);
                if (u == i) {
                    continue;
                }
                vec.push_back(u);
            }
            vec.push_back(vid);
            std::sort(vec.begin(), vec.end()); 
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());

            auto new_v = new tVertex(vid, vid);
            for (int u : vec) {
                lock(u);
            }

            if (vertex(vid) == nullptr) {
                vertex(vid) = new_v;
                for (int u : vec) {
                    if (vertex(u) == nullptr) continue;
                    if (u == vid) continue;
                    Relation *r = new Relation(vid, u, -1);
                    auto p = make_pair(vid,u);
                    source(vid).emplace(p,r);
                    destination(u).emplace(p,r);
                }
            } else {
                retval = false;
            }

            for (auto u = vec.rbegin(); u != vec.rend(); u++) {
                if (vertex(vid) != nullptr && vertex(*u) != nullptr) inc_seq(*u);
                unlock(*u);
            }
            if(retval==false){
                delete(new_v);
            }
            return retval;
        }

        bool remove_vertex(int vid) {
startOver:
            {
                // Step 1: Acquire vertex and collect neighbors...
                std::vector<int> vertices;
                lock(vid);
                if (vertex(vid) == nullptr) {
                    unlock(vid);
                    return false;
                }
                uint32_t seq = get_seq(vid);
                for (auto r : source(vid)) {
                    vertices.push_back(r.second->dest);
                }
                for (auto r : destination(vid)) {
                    vertices.push_back(r.second->src);
                }
                
                unlock(vid);
                vertices.push_back(vid);
                std::sort(vertices.begin(), vertices.end()); 
                vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());

                // Step 2: Acquire lock-order...
                for (int _vid : vertices) {
                    lock(_vid);
                    if (vertex(_vid) == nullptr && get_seq(vid) == seq) {
                        for (auto r : source(vid)) {
                            if (r.second->dest == _vid)
                            std::cout << "(" << r.second->src << "," << r.second->dest << ")" << std::endl;
                        }
                        for (auto r : destination(vid)) {
                            if (r.second->src == _vid)
                            std::cout << "(" << r.second->src << "," << r.second->dest << ")" << std::endl;
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

                    auto src = make_pair(other, vid);
                    auto dest = make_pair(vid, other);
                    if (!has_relation(source(other), src) && !has_relation(destination(other), dest)) {
                        std::cout << "Observed pair (" << vid << "," << other << ") that was originally there but no longer is..." << std::endl;
                        for (auto r : source(vid)) {
                            if (r.second->dest == other)
                            std::cout << "Us: (" << r.second->src << "," << r.second->dest << ")" << std::endl;
                        }
                        for (auto r : destination(other)) {
                            if (r.second->src == vid) {
                                std::cout << "Them: (" << r.second->src << "," << r.second->dest << ")" << std::endl;
                            }
                        }
                        for (auto r : destination(vid)) {
                            if (r.second->src == other) {
                                std::cout << "Us: (" << r.second->src << "," << r.second->dest << ")" << std::endl;
                            }
                        }
                        for (auto r : source(other)) {
                            if (r.second->dest == vid) {
                                std::cout << "Them: (" << r.second->src << "," << r.second->dest << ")" << std::endl;
                            }
                        }
                        std::abort();
                    }
                    auto ret1 = remove_relation(source(other), src);// this may fail
                    auto ret2 = remove_relation(destination(other), dest);// this may fail
                    if(ret1!=nullptr){
                        delete(ret1);// only deallocate relation removed from source
                    }
                    assert(!has_relation(source(other), src) && !has_relation(destination(other), dest));
                }
                

                for (auto r : source(vid)) delete(r.second);
                source(vid).clear();
                destination(vid).clear();
                destroy(vid);

                // Step 4: Release in reverse order
                for (auto _vid = vertices.rbegin(); _vid != vertices.rend(); _vid++) {
                    inc_seq(*_vid);
                    unlock(*_vid);
                }
            }
            return true;
        }
        void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
            Persistent::init_thread(gtc, ltc);
        }
    private:
        tVertex *& vertex(size_t idx) {
            return vMeta[idx].idxToVertex;
        }

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
            assert(vertex(idx)!=nullptr);
            delete vertex(idx);
            vertex(idx) = nullptr;
        }

        // Incoming edges
        Map& source(int idx) {
            return vertex(idx)->adjacency_list;

        }

        // Outgoing edges
        Map& destination(int idx) {
            return vertex(idx)->dest_list;
        }

        bool has_relation(Map& set, pair<int,int>& r) {
            auto search = set.find(r);
            return search != set.end();
        }

        Relation* remove_relation(Map& set, pair<int,int>& r) {
            // remove relation from set but NOT deallocate it
            // return Relation* in the set
            auto search = set.find(r);
            if (search != set.end()) {
                Relation *tmp = search->second;
                set.erase(search);
                return tmp;
            }
            return nullptr;
        }
};

template <size_t numVertices = 1024, size_t meanEdgesPerVertex=20, size_t vertexLoad = 50>
class NVMGraphFactory : public RideableFactory{
    Rideable *build(GlobalTestConfig *gtc){
        return new NVMGraph<numVertices, meanEdgesPerVertex, vertexLoad>(gtc);
    }
};
// #pragma GCC reset_options

#endif

