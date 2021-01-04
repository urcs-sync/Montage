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
      std::cout << boost::format("bin %i [ %.1f, %.1f ): %i\n")
        % x.index() % x.bin().lower() % x.bin().upper() % *x;
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
        }

        int execute(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            int tid = ltc->tid;
            std::mt19937_64 gen_p(ltc->seed);
            for (size_t i = 0; i < thd_ops[ltc->tid]; i++) {
                // Sketch:
                // 1) Obtain metrics for average degree, make decision based on
                // degree distribution, i.e. keep an array of pairs that are collected
                // based on potential connectivity of the graph.
                // 2) Use said metrics and data structures obtained from metrics to do insertions
                // and removals in phases, split among threads.
                // 3) Between each phase, print out metrics; log |V|, |E|, average degree, and maximum degree as a 
                // datapoint to be plot later.
            }
            return thd_ops[ltc->tid];
        }

        void cleanup(GlobalTestConfig *gtc) {
            delete g;
        }

        void parInit(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            g->init_thread(gtc, ltc);
            usleep(1 * 1000 * 1000);
        }
};
#endif
