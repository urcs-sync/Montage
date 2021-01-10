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
template <size_t numVertices = 1024, size_t meanEdgesPerVertex=20, size_t vertexLoad=50>
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
            int get_lbl() const { return this->lbl; }
            void set_id(int id) { this->id = id; }
            int get_id() const { return this->id; }
            void persist();
        };

        class Relation : public Persistent {
            int weight;
            int src;
            int dest;
            public:
            Relation(){}
            Relation(int src, int dest, int weight): weight(weight), src(src), dest(dest){}
            Relation(const Relation& oth): weight(oth.weight), src(oth.src), dest(oth.dest){}
            void set_weight(int weight) { this->weight = weight; }
            int get_weight() const { return this->weight; }
            int get_src() const { return this->src; }
            int get_dest() const { return this->dest; }
            bool operator==(const Relation* other) const {
                return this->get_src() == other->get_src() && this->get_dest() == other->get_dest();
            }

            void persist(){}
        };

        struct RelationHash {
            std::size_t operator()(const Relation *r) const {
                return std::hash<int>()(r->get_src()) ^ std::hash<int>()(r->get_dest());
            }
        };

        struct RelationEqual {
            bool operator()(const Relation *r1, const Relation *r2) const {
                return r1->get_src() == r2->get_src() && r1->get_dest() == r2->get_dest();
            }
        };

        using Set = std::unordered_set<Relation*,RelationHash,RelationEqual>;

        class tVertex {
            public:
                Set adjacency_list;
                Set dest_list;
                Vertex* payload;
                int id; // Immutable, so we can keep transient copy.
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

        struct alignas(64) VertexMeta {
            tVertex* idxToVertex;// Transient set of transient vertices to index map
            std::mutex vertexLocks;// Transient locks for transient vertices
            uint32_t vertexSeqs;// Transient sequence numbers for transactional operations on vertices
        };


        // Allocates data structures and pre-loads the graph
        NVMGraph(GlobalTestConfig* gtc) {
            Persistent::init();
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
                    vMeta[i].idxToVertex = new tVertex(i,i);
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

        ~NVMGraph() {
            Persistent::finalize();
        }

        void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
            Persistent::init_thread(gtc, ltc);
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
                if (vertex(i) == nullptr) continue;
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
            if (vertex(src) == nullptr || vertex(dest) == nullptr) {
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
            if (vertex(src) == nullptr) {
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
            
            if (vertex(src) != nullptr && vertex(dest) != nullptr) {
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

            if (vertex(vid) == nullptr) {
                vertex(vid) = new tVertex(vid, vid);
                for (int u : vec) {
                    if (vertex(u) == nullptr) continue;
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
                if (vertex(vid) != nullptr && vertex(*u) != nullptr) inc_seq(*u);
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
                if (vertex(vid) == nullptr) {
                    unlock(vid);
                    return false;
                }
                uint32_t seq = get_seq(vid);
                for (auto r : source(vid)) {
                    vertices.push_back(r->get_dest());
                }
                for (auto r : destination(vid)) {
                    vertices.push_back(r->get_src());
                }
                
                vertices.push_back(vid);
                std::sort(vertices.begin(), vertices.end()); 
                vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());

                // Step 2: Release lock, then acquire lock-order...
                unlock(vid);
                for (int _vid : vertices) {
                    lock(_vid);
                    if (vertex(_vid) == nullptr && get_seq(vid) == seq) {
                        for (auto r : source(vid)) {
                            if (r->get_dest() == _vid)
                            std::cout << "(" << r->get_src() << "," << r->get_dest() << ")" << std::endl;
                        }
                        for (auto r : destination(vid)) {
                            if (r->get_src() == _vid)
                            std::cout << "(" << r->get_src() << "," << r->get_dest() << ")" << std::endl;
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
                    if (!has_relation(source(other), &src) && !has_relation(destination(other), &dest)) {
                        std::cout << "Observed pair (" << vid << "," << other << ") that was originally there but no longer is..." << std::endl;
                        for (auto r : source(vid)) {
                            if (r->get_dest() == other)
                            std::cout << "Us: (" << r->get_src() << "," << r->get_dest() << ")" << std::endl;
                        }
                        for (auto r : destination(other)) {
                            if (r->get_src() == vid) {
                                std::cout << "Them: (" << r->get_src() << "," << r->get_dest() << ")" << std::endl;
                            }
                        }
                        for (auto r : destination(vid)) {
                            if (r->get_src() == other) {
                                std::cout << "Us: (" << r->get_src() << "," << r->get_dest() << ")" << std::endl;
                            }
                        }
                        for (auto r : source(other)) {
                            if (r->get_dest() == vid) {
                                std::cout << "Them: (" << r->get_src() << "," << r->get_dest() << ")" << std::endl;
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
        Set& source(int idx) {
            return vertex(idx)->adjacency_list;

        }

        // Outgoing edges
        Set& destination(int idx) {
            return vertex(idx)->dest_list;
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
class NVMGraphFactory : public RideableFactory{
    Rideable *build(GlobalTestConfig *gtc){
        return new NVMGraph<numVertices, meanEdgesPerVertex, vertexLoad>(gtc);
    }
};
// #pragma GCC reset_options

#endif
