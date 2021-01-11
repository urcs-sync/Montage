#ifndef GRAPH_TEST_HPP
#define GRAPH_TEST_HPP

// Louis Jenkins & Benjamin Valpey
#include <cstdint>
#include <random>
#include <chrono>
#include <algorithm>
#include <array>
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
        int max_verts;
        int addEdgeProb;
        int remEdgeProb;
        int addVerProb;
        int remVerProb;
        int desiredAvgDegree;
        int vertexLoad;
        std::atomic<int> workingThreads;
        std::atomic<int> threadsDone;
        padded<std::array<int,4>>* operations;


        GraphTest(uint64_t numOps, int max_verts, int desiredAvgDegree, int vertexLoad) :
            total_ops(numOps), max_verts(max_verts), desiredAvgDegree(desiredAvgDegree), vertexLoad(vertexLoad) {
        }

        void init(GlobalTestConfig *gtc) {
            uint64_t new_ops = total_ops / gtc->task_num;
            operations = new padded<std::array<int,4>>[gtc->task_num];
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
            int edge_op = 6666;
            if(gtc->checkEnv("EdgeOp")){
                edge_op = atoi((gtc->getEnv("EdgeOp")).c_str());
                assert(edge_op>=0 && edge_op<=10000);
            }
            // addEdgeProb = std::max(1,(int)edge_op*desiredAvgDegree*100/(max_verts*vertexLoad));
            // remEdgeProb = std::max(1,edge_op-addEdgeProb);
            // (FIXME) Wentao: I don't know why 3:1 is the ratio for being
            // stable. Find the root cause and solve it later.
            addEdgeProb = edge_op*3/4;
            remEdgeProb = edge_op/4;
            addVerProb = (10000-edge_op)/2;
            remVerProb = 10000-edge_op-addVerProb;
            // Printing out real ratio of operations
            std::cout<<"AddEdge:RemoveEdge:AddVertex:RemoveVertex="<<addEdgeProb<<":"<<remEdgeProb<<":"<<addVerProb<<":"<<remVerProb<<std::endl;
            workingThreads = gtc->task_num;
            threadsDone = 0;
        }

        int execute(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            int tid = ltc->tid;
            if (tid == 0) std::cout << "Starting test now..." << std::endl;
            std::mt19937_64 gen_p(ltc->seed);
            std::mt19937_64 gen_v(ltc->seed + 1);
            std::uniform_int_distribution<> dist(0,9999);
            std::uniform_int_distribution<> distv(0,max_verts-1);
            for (size_t i = 0; i < thd_ops[tid]; i++) {
            	int rng = dist(gen_p);
                if (rng < addEdgeProb) {
                    // std::cout << "rng(" << rng << ") is add_edge <= " << addEdgeProb << std::endl; 
                    if(g->add_edge(distv(gen_v), distv(gen_v), -1))
                        operations[tid].ui[0]++;
                } else if (rng < addEdgeProb + remEdgeProb) {
                    // std::cout << "rng(" << rng << ") is remove_any_edge <= " << addEdgeProb + remEdgeProb << std::endl; 
                    if(g->remove_edge(distv(gen_v), distv(gen_v)))
                        operations[tid].ui[1]++;
                } else if (rng < addEdgeProb + remEdgeProb + addVerProb) {
                    // std::cout << "rng(" << rng << ") is has_edge <= " << addEdgeProb + remEdgeProb + addVerProb << std::endl; 
                    if(g->add_vertex(distv(gen_v)))
                        operations[tid].ui[2]++;
                } else {
                    // std::cout << "rng(" << rng << ") is remove_vertex..."; 
                    if(g->remove_vertex(distv(gen_v)))
                        operations[tid].ui[3]++;
                }
            }
            return thd_ops[ltc->tid];
        }

        void cleanup(GlobalTestConfig *gtc) {
            auto stats = g->grab_stats();
            std::apply(print_stats, stats);
            size_t total=0,add_edge=0,rem_edge=0,add_ver=0,rem_ver=0;
            for(int i=0;i<gtc->task_num;i++){
                total += (operations[i].ui[0] + operations[i].ui[1] + operations[i].ui[2] + operations[i].ui[3]);
                add_edge += operations[i].ui[0];
                rem_edge += operations[i].ui[1];
                add_ver += operations[i].ui[2];
                rem_ver += operations[i].ui[3];
            }
            delete operations;
	    double add_edge_prop = add_edge*100 / (double) total;
	    double rem_edge_prop = rem_edge*100 / (double) total;
	    double add_ver_prop = add_ver*100 / (double) total;
	    double rem_ver_prop = rem_ver*100 / (double) total;
        // Printing out ratio of successful operations
	    std::cout << "add_edge = " << add_edge << " (" << add_edge_prop << "%)" << std::endl
		   << ", remove_edge = " << rem_edge << " (" << rem_edge_prop << "%)" << std::endl
		   << ", add_vertex = " << add_ver << " (" << add_ver_prop << "%)" << std::endl
		   << ", remove_vertex = " << rem_ver << " (" << rem_ver_prop << "%)" << std::endl;
            delete g;
        }

        void parInit(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            g->init_thread(gtc, ltc);
        }
};
#endif
