#ifndef GRAPH_TEST_HPP
#define GRAPH_TEST_HPP

// Louis Jenkins & Benjamin Valpey
#include <cstdint>
#include <random>
#include <chrono>
#include "RGraph.hpp"
#include <omp.h>

void ErdosRenyi(RGraph *g, int numVertices, double p=0.5) {
    size_t x = numVertices;
    size_t numEdges = (x * x) * p;
    #pragma omp parallel 
    {
        std::mt19937_64 gen_p(std::chrono::system_clock::now().time_since_epoch().count() + omp_get_thread_num());
        pds::init_thread(omp_get_thread_num());
        #pragma omp for
        for (size_t i = 0; i < numEdges; i++) {
            g->add_edge(gen_p() % numVertices, gen_p() % numVertices, 1);
        }
    }
}

class GraphTest : public Test {
    public:
        RGraph *g;
        uint64_t total_ops;
        uint64_t *thd_ops;
        uint64_t max_verts;
        int pi;
        int pr;
        int pl;
        int pc;
        unsigned prop_inserts, prop_removes, prop_lookup, prop_clear;

        GraphTest(uint64_t numOps, uint64_t max_verts, int insertP, int removeP, int lookupP, int clearP) :
            total_ops(numOps), max_verts(max_verts), pi(insertP), pr(removeP), pl(lookupP), pc(clearP) {
                if (insertP + removeP + lookupP + clearP != 100) {
                    errexit("Probability of insert/remove/lookup must accumulate to 100!");
                }
                prop_inserts = pi;
                prop_removes = pi + pr;
                prop_lookup = pi + pr + pl;
                prop_clear = pi + pr + pl + pc;
            }

        void init(GlobalTestConfig *gtc) {
            // Persistent::init();
            // pds::init(gtc);
            uint64_t new_ops = total_ops / gtc->task_num;
            thd_ops = new uint64_t[gtc->task_num];
            for (int i = 0; i<gtc->task_num; i++) {
                thd_ops[i] = new_ops;
            }
            if (new_ops * gtc->task_num != total_ops) {
                thd_ops[0] += (total_ops - new_ops * gtc->task_num);
            }

            Rideable* ptr = gtc->allocRideable();
            g = dynamic_cast<RGraph*>(ptr);
            if(!g){
                errexit("GraphTest must be run on RGraph type object.");
            }
            
            /* set interval to inf so this won't be killed by timeout */
            gtc->interval = numeric_limits<double>::max();
        }

        int execute(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            int tid = ltc->tid;
            std::mt19937_64 gen_p(ltc->seed);
            std::mt19937_64 gen_v(ltc->seed + 1);
            for (size_t i = 0; i < thd_ops[ltc->tid]; i++) {
                unsigned p = gen_p()%100;
                int src = gen_v() % max_verts;
                int dest = gen_v() % max_verts;
                if (p<prop_inserts) {
                    //std::cout << "Insert(" << src << ", " << dest << ")" << std::endl;
                    g->add_edge(src, dest, 1);
                } else if (p<prop_removes) {
                    //std::cout << "Delete(" << src << ", " << dest << ")" << std::endl;
                    g->for_each_edge(src, [&dest](int v) { dest = v; return false; });
                    g->remove_edge(src, dest);
                } else if (p < prop_lookup) {
                    //std::cout << "Lookup(" << src << "," << dest << ")" << std::endl;
                    g->has_edge(src, dest);
                } else {
                    g->clear_vertex(src);
                }
            }
            return thd_ops[ltc->tid];
        }

        void cleanup(GlobalTestConfig *gtc) {
            pds::finalize();
            Persistent::finalize();
        }

        void parInit(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            // pds::init_thread(ltc->tid);
            size_t x = max_verts;
            size_t numEdges = (x * x) * 0.5;
            std::random_device rd;
            std::mt19937_64 gen_p(std::chrono::system_clock::now().time_since_epoch().count() + ltc->tid);
            std::uniform_int_distribution<> distrib(0, max_verts - 1);
            std::normal_distribution<double> norm(10,3);
            std::default_random_engine generator;
            for (uint64_t i = ltc->tid; i < max_verts; i += gtc->task_num) {
                int n = max(1, (int) round(norm(generator)));
                for (int j = 0; j < n; j++) {
                    uint64_t k = round(distrib(gen_p));
                    while (k == i) k = round(distrib(gen_p));
                    g->add_edge(i, k, 1);
                }
            }
        }
};
#endif
