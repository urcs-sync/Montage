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
        int max_verts;
        int addEdgeProb;
        int remEdgeProb;
        int addVerProb;
        int remVerProb;
        int desiredAvgDegree;
        int vertexLoad;
        int edge_op;
        std::atomic<int> workingThreads;
        std::atomic<int> threadsDone;
        padded<std::array<int,4>>* operations;


        GraphTest(int max_verts, int desiredAvgDegree, int vertexLoad, int edge_op) :
            max_verts(max_verts), desiredAvgDegree(desiredAvgDegree), vertexLoad(vertexLoad), edge_op(edge_op) {
        }

        void init(GlobalTestConfig *gtc) {
            operations = new padded<std::array<int,4>>[gtc->task_num];

            Rideable* ptr = gtc->allocRideable();
            g = dynamic_cast<RGraph*>(ptr);
            if(!g){
                errexit("GraphTest must be run on RGraph type object.");
            }

            auto stats = g->grab_stats();
            if(gtc->verbose) std::apply(print_stats, stats);
            addEdgeProb = std::max(1,(int)edge_op*desiredAvgDegree*100/(max_verts*vertexLoad));
            remEdgeProb = std::max(1,edge_op-addEdgeProb);
            addVerProb = (10000-edge_op)/2;
            remVerProb = 10000-edge_op-addVerProb;
            // Printing out real ratio of operations
            if(gtc->verbose) std::cout<<"AddEdge:RemoveEdge:AddVertex:RemoveVertex="<<addEdgeProb<<":"<<remEdgeProb<<":"<<addVerProb<<":"<<remVerProb<<std::endl;
            workingThreads = gtc->task_num;
            threadsDone = 0;
        }

        int execute(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            auto time_up = gtc->finish;
            int ops = 0;
            int tid = ltc->tid;
            if (tid == 0) if(gtc->verbose) std::cout << "Starting test now..." << std::endl;
            std::mt19937_64 gen_p(ltc->seed);
            std::mt19937_64 gen_v(ltc->seed + 1);
            std::uniform_int_distribution<> dist(0,9999);
            std::uniform_int_distribution<> distv(0,max_verts-1);
            auto now = std::chrono::high_resolution_clock::now();
            while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){
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
                ops++;
                if (ops % 512 == 0){
                    now = std::chrono::high_resolution_clock::now();
                }
            }
            return ops;
        }

        void cleanup(GlobalTestConfig *gtc) {
            auto stats = g->grab_stats();
            if(gtc->verbose) std::apply(print_stats, stats);
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
            if(gtc->verbose) std::cout << "add_edge = " << add_edge << " (" << add_edge_prop << "%)" << std::endl
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
