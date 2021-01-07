#ifndef GRAPH_TEST_HPP
#define GRAPH_TEST_HPP

// Louis Jenkins & Benjamin Valpey
#include <cstdint>
#include <random>
#include <chrono>
#include "RGraph.hpp"
#include "Recoverable.hpp"
#include <omp.h>
#include <boost/histogram.hpp>
#include <boost/format.hpp>
#include <unistd.h>
#include <cmath>

static void print_stats(int numV, int numE, double averageDegree, int *vertexDegrees, int vertexDegreesLength) {
    int maxDegree = 0;
    for (auto i = 0; i < vertexDegreesLength; i++) {
        maxDegree = max(maxDegree, vertexDegrees[i]);
    }
    std::cout << "|V|=" << numV << ",|E|=" << numE << ",average degree = " << averageDegree << ",maximum degree = " << maxDegree << std::endl;
    using namespace boost::histogram;

    auto h = make_histogram(axis::regular<>(maxDegree, 0, maxDegree));
    for (auto i = 0; i < vertexDegreesLength; i++) {
        h(vertexDegrees[i]);
    }
    for (auto&& x : indexed(h)) {
        if (*x == 0) continue;
      std::cout << boost::format("bin %i: %i\n")
        % x.index() % *x;
    }
}

class GraphTest : public Test {
    public:
        RGraph *g;
        uint64_t total_ops;
        uint64_t *thd_ops;
        uint64_t max_verts;
        int insertionProb;
        int removalProb;
        int lookupProb;
        int clearProb;
        int desiredAvgDegree;
        std::atomic<int> workingThreads;
        std::atomic<int> threadsDone;


        GraphTest(uint64_t numOps, uint64_t max_verts, int desiredAvgDegree) :
            total_ops(numOps), max_verts(max_verts), desiredAvgDegree(desiredAvgDegree) {
        }

        void init(GlobalTestConfig *gtc) {
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
            auto stats = g->grab_stats();
            std::apply(print_stats, stats);
            insertionProb = 3300;
            removalProb = 3300;
            lookupProb = 1650;
            clearProb = 1650;
            workingThreads = gtc->task_num;
            threadsDone = 0;
        }

        int execute(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            int tid = ltc->tid;
            std::mt19937_64 gen_p(ltc->seed);
            std::mt19937_64 gen_v(ltc->seed + 1);
            std::uniform_int_distribution<> dist(0,9999);
            std::uniform_int_distribution<> distv(0,max_verts-1);
            int rng = dist(gen_p);
            for (size_t i = 0; i < thd_ops[tid]; i++) {
                if (rng <= insertionProb) {
                    // std::cout << "rng(" << rng << ") is add_edge <= " << insertionProb << std::endl; 
                    g->add_edge(distv(gen_v), distv(gen_v), -1);
                } else if (rng <= insertionProb + removalProb) {
                    // std::cout << "rng(" << rng << ") is remove_any_edge <= " << insertionProb + removalProb << std::endl; 
                    g->remove_edge(distv(gen_v), distv(gen_v));
                } else if (rng <= insertionProb + removalProb + lookupProb) {
                    // std::cout << "rng(" << rng << ") is has_edge <= " << insertionProb + removalProb + lookupProb << std::endl; 
                    g->add_vertex(distv(gen_v));
                } else {
                    // std::cout << "rng(" << rng << ") is remove_vertex..."; 
                    g->remove_vertex(distv(gen_v));
                }
            }
            return thd_ops[ltc->tid];
        }

        void cleanup(GlobalTestConfig *gtc) {
            auto stats = g->grab_stats();
            std::apply(print_stats, stats);
            delete g;
        }

        void parInit(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            g->init_thread(gtc, ltc);
        }
};
#endif
