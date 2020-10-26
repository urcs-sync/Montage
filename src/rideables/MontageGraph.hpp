/**
 * Author:      Benjamin Valpey & Louis Jenkins
 * Date:        29 July 2020
 * Filename:    MontageGraph.hpp
 * Description: Adaptation of fine-grained concurrent graph to Montage.
 */

#ifndef MONTAGEGRAPH_HPP
#define MONTAGEGRAPH_HPP

#include "TestConfig.hpp"
#include "CustomTypes.hpp"
#include "ConcurrentPrimitives.hpp"
#include "RGraph.hpp"
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include "persist_struct_api.hpp"
#include <omp.h>
#include <cassert>

using namespace pds;

/**
 * SimpleGraph class.  Labels are of templated type K.
 */
template <size_t numVertices = 1024>
class MontageGraph : public RGraph, public Recoverable{

    public:

        class tVertex;
        class BasePayload : public PBlk {
        protected:
            GENERATE_FIELD(int, tag, BasePayload);
        public:
            BasePayload(){}
            BasePayload(const BasePayload& oth): PBlk(oth){}
            void persist();

        };
        class Vertex : public BasePayload {
            GENERATE_FIELD(int, id, Vertex);
            GENERATE_FIELD(int, lbl, Vertex);
        public:
            Vertex(){this->m_tag = 0;}
            Vertex(int id, int lbl): m_id(id), m_lbl(lbl){this->m_tag = 0;}
            Vertex(const Vertex& oth): BasePayload(oth), m_id(oth.m_id), m_lbl(oth.m_lbl) {this->m_tag = 0;}
            bool operator==(const Vertex& oth) const { return m_id==oth.m_id;}
        };

        class Relation : public BasePayload {
            GENERATE_FIELD(int, weight, Relation);
            GENERATE_FIELD(int, src, Relation);
            GENERATE_FIELD(int, dest, Relation);
        public:
            Relation(){this->m_tag = 1;}
            Relation(Vertex* src, Vertex* dest, int weight): m_weight(weight), m_src(src->id), m_dest(dest->id){this->m_tag = 1;}
            Relation(tVertex *src, tVertex *dest, int weight): m_weight(weight), m_src(src->get_id()), m_dest(dest->get_id()){this->m_tag = 1;}
            Relation(const Relation& oth): BasePayload(oth), m_weight(oth.m_weight), m_src(oth.m_src), m_dest(oth.m_dest){this->m_tag = 1;}
        };
        class tVertex {
            public:
                Vertex *payload = nullptr;

                int id; // cached id
                uint64_t seqNumber; 
                std::unordered_set<Relation*> adjacency_list;

                std::unordered_set<Relation*> dest_list;

                std::mutex lck;

                tVertex(int id, int lbl) {
                    payload = PNEW(Vertex, id, lbl);
                    this->id = id;
                }
                tVertex(Vertex* p) {
                    // Use this method for recovery to avoid having to call PNEW when the block already exists.
                    payload = p;
                    this->id = p->get_unsafe_id();
                }

                ~tVertex() {
                    if (payload){
                        PDELETE(payload);
                    }
                }

                void set_lbl(int l) {
                    payload = payload->set_lbl(l);
                }
                int get_lbl() {
                    return payload->get_lbl();
                }

                // Immutable
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

        // Need to create our own struct becuase std::tuple is not trivially copyable
        struct RelationWrapper {
            int v1;
            int v2;
            Relation *e;
        };

        MontageGraph(GlobalTestConfig* gtc) : Recoverable(gtc) {
            BEGIN_OP_AUTOEND();
            idxToVertex = new tVertex*[numVertices];
            // Initialize...
            for (size_t i = 0; i < numVertices; i++) {
                idxToVertex[i] = new tVertex(i, -1);
            }
        }

        void init_thread(GlobalTestConfig* gtc, LocalTestConfig* ltc){
            Recoverable::init_thread(gtc, ltc);
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
                    PDELETE(r);
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
                BEGIN_OP_AUTOEND();
                Relation* r = PNEW(Relation,v1, v2, weight);
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
                BEGIN_OP_AUTOEND();
                if (std::any_of(v->adjacency_list.begin(), v->adjacency_list.end(), 
                            [=] (Relation *r) { return r->get_unsafe_dest() == v2; })) {
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
                BEGIN_OP_AUTOEND();
                // Scan v1 for an edge containing v2 in its adjacency list...
                Relation *rdel = nullptr;
                for (Relation *r : v1->adjacency_list) {
                    if (r->get_unsafe_dest() == v2->get_id()) {
                        rdel = r;
                        v1->adjacency_list.erase(r);
                        break;
                    }
                }
            
                if (rdel){
                    v2->dest_list.erase(rdel);
                    PDELETE(rdel);
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
            BEGIN_OP_AUTOEND();
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
            BEGIN_OP_AUTOEND();
            bool retval = false;
            /*
            tVertex *v = idxToVertex[src];
            v->lock();    
            for (auto itr = v->adjacency_list.begin(); itr != v->adjacency_list.end(); itr++){
                    if ((*itr)->get_unsafe_dest()->get_unsafe_id() == dest) {
                        *itr = (*itr)->set_weight(w);
                        retval = true;
                        break;
                    }
            }
            v->unlock();*/
            return retval;
        }

        int recover(bool simulated) {
            if (simulated) {
                pds::recover_mode();
                delete idxToVertex;
                idxToVertex = new tVertex*[numVertices];
                #pragma omp parallel for
                for (size_t i = 0; i < numVertices; i++) {
                    idxToVertex[i] = nullptr;
                }
                pds::online_mode();
            }

            int block_cnt = 0;
            auto begin = chrono::high_resolution_clock::now();
            std::unordered_map<uint64_t, PBlk*>* recovered = pds::recover(); 
            auto end = chrono::high_resolution_clock::now();
            auto dur = end - begin;
            auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            std::cout << "Spent " << dur_ms << "ms getting PBlk(" << recovered->size() << ")" << std::endl;
            
            begin = chrono::high_resolution_clock::now();
            std::vector<Relation*> relationVector;
            std::vector<Vertex*> vertexVector;
            {
                BEGIN_OP_AUTOEND();
                for (auto itr = recovered->begin(); itr != recovered->end(); ++itr) {
                    // iterate through all recovered blocks.  Sort the blocks into vectors containing the different
                    // payloads to be iterated over later.
                    block_cnt ++;


                    // Should these be parallel?  I'm not sure..
                    BasePayload* b = reinterpret_cast<BasePayload*>(itr->second);

                    switch (b->get_unsafe_tag()) {
                        case 0:
                            {
                                Vertex* v = reinterpret_cast<Vertex*>(itr->second);
                                vertexVector.push_back(v);
                                break;
                            }
                        case 1:
                            {
                                Relation* r = reinterpret_cast<Relation*>(itr->second);
                                relationVector.push_back(r);
                                break;
                            }
                        default:
                            {
                                std::cerr << "Found bad tag " << b->get_unsafe_tag() << std::endl;
                            }
                    }
                }
            }
            end = chrono::high_resolution_clock::now();
            dur = end - begin;
            dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            std::cout << "Spent " << dur_ms << "ms gathering vertices(" << vertexVector.size() << ") and edges(" << relationVector.size() << ")..." << std::endl;
            begin = chrono::high_resolution_clock::now();
            #pragma omp parallel
            {
                pds::init_thread(omp_get_thread_num());
                #pragma omp for
                for (size_t i = 0; i < vertexVector.size(); ++i) {
                    int id = vertexVector[i]->get_unsafe_id();
                    if (idxToVertex[id] != nullptr) {
                        std::cerr << "Somehow recovered vertex " << id << " twice!" << std::endl;
                        continue;
                    }
                    tVertex* new_node = new tVertex(vertexVector[i]);
                    idxToVertex[id] = new_node;
                }
            }
            end = chrono::high_resolution_clock::now();
            dur = end - begin;
            dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            std::cout << "Spent " << dur_ms << "ms creating vertices..." << std::endl;
            begin = chrono::high_resolution_clock::now();
            
            int num_threads;
            #pragma omp parallel
            num_threads = omp_get_num_threads(); 
            
            // Vertices are cyclically distributed across all OpenMP threads, such that a thread t_i
            // owns vertices i, i + T, i + 2T, etc., where T is the number of OpenMP threads.
            // The vector of edges is iterated over in parallel and given an edge e=(v,v'), if
            // v belongs to thread t_i, t_i will add it to the source set of v; if v' belongs to
            // thread t_i, t_i will add v' to the destination set of v'; if t_i does not own it, nothing happens
            std::vector<RelationWrapper> *buffers = new std::vector<RelationWrapper>[num_threads * num_threads];
            #pragma omp parallel
            {
                int tid = omp_get_thread_num();
                pds::init_thread(tid);
                #pragma omp for
                for (size_t i = 0; i < relationVector.size(); ++i) {
                    Relation *e = relationVector[i];
                    int id1 = e->get_unsafe_src();
                    int id2 = e->get_unsafe_dest();
                    RelationWrapper item = { id1, id2, e };
                    if (id1 < 0 || (size_t) id1 >= numVertices || id2 < 0 ||  (size_t) id2 >= numVertices) {
                        std::cerr << "Found a relation with a bad edge: (" << id1 << "," << id2 << ")" << std::endl;
                        continue; 
                    }

                    tVertex *v1 = idxToVertex[id1];
                    tVertex *v2 = idxToVertex[id2];   
                    if (v1 == nullptr || v2 == nullptr) {
                        std::cerr << "Edge (" << id1 << ", " << id2 << ") has nullptr(v1=" << (v1 == nullptr) << ", v2=" << (v2 == nullptr) << ")" << std::endl;
                        continue; 
                    }

                    buffers[tid * num_threads + (id1 % num_threads)].push_back(item);
                    if ((id2 % num_threads) != (id1 % num_threads)) {
                        buffers[tid * num_threads + (id2 % num_threads)].push_back(item);
                    }
                }
                
                std::vector<RelationWrapper> tpls;
                size_t size = 0;
                for (int _tid = 0; _tid < num_threads; _tid++) {
                    size += buffers[_tid * num_threads + tid].size();
                }
                tpls.resize(size);
                
                size_t offset = 0;
                for (int _tid = 0; _tid < num_threads; _tid++) {
                    auto *buffer = &buffers[_tid * num_threads + tid];
                    std::copy(buffer->begin(), buffer->end(), tpls.begin() + offset);
                    offset += buffer->size();
                }
                
                #pragma omp barrier
                #pragma omp master
                delete[] buffers; 

                std::sort(tpls.begin(), tpls.end(), [](RelationWrapper r1, RelationWrapper r2) { return r1.v1 < r2.v1; });
                for (auto r : tpls) {
                    int v1 = r.v1;
                    Relation *e = r.e;
                    if (v1 % num_threads == tid) {
                        idxToVertex[v1]->adjacency_list.insert(e);
                    }
                }
                std::sort(tpls.begin(), tpls.end(), [](RelationWrapper r1, RelationWrapper r2) { return r1.v2 < r2.v2; });
                for (auto r : tpls) {
                    int v2 = r.v2;
                    Relation *e = r.e;
                    if (v2 % num_threads == tid) {
                        idxToVertex[v2]->dest_list.insert(e);
                    }
                }
            }

            end = chrono::high_resolution_clock::now();
            dur = end - begin;
            dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            std::cout << "Spent " << dur_ms << "ms forming edges..." << std::endl;
            begin = chrono::high_resolution_clock::now();

            delete recovered;
            return block_cnt;
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
                    vertices.push_back(r->get_unsafe_dest());
                }
                for (Relation *r : v->dest_list) {
                    vertices.push_back(r->get_unsafe_src());
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
                        if (r->get_unsafe_src() == id) {
                            toRemoveList.push_back(r);
                        }
                    }
                    for (Relation *r : toRemoveList) {
                        _v->adjacency_list.erase(r);
                        garbageList.push_back(r);
                    }
                    toRemoveList.clear();

                    for (Relation *r : _v->dest_list) {
                        if (r->get_unsafe_dest() == id) {
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
                {
                    BEGIN_OP_AUTOEND()
                    for (Relation *r : garbageList) {
                        PDELETE(r);
                    }
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
                if (!fn(r->get_unsafe_dest())) {
                    break;
                }
            }
            idxToVertex[v]->unlock();
        }

};

template <size_t order = 1024>
class MontageGraphFactory : public RideableFactory{
    Rideable *build(GlobalTestConfig *gtc){
        return new MontageGraph<order>(gtc);
    }
};


#endif
