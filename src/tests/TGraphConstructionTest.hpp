#ifndef TGRAPHCONSTRUCTIONTEST_HPP
#define TGRAPHCONSTRUCTIONTEST_HPP

// Louis Jenkins & Benjamin Valpey
#include <cstdint>
#include <random>
#include <chrono>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sstream>
#include <iomanip>
#include "../rideables/MontageGraph.hpp"
#include "../rideables/TGraph.hpp"
#include "../rideables/NVMGraph.hpp"
#include "../rideables/DLGraph.hpp"
#include <pthread.h>

class TGraphConstructionTest : public Test {
public:
    RGraph *g;
    uint64_t total_ops;
    uint64_t *thd_ops;
    uint64_t max_verts;
    string graphDir;
    string base_fname;
    int num_files;
    int file_id_width;
    bool verify;

    TGraphConstructionTest(string graphDir, string base_fname, int num_files, int width) : graphDir(graphDir), base_fname(base_fname), num_files(num_files), file_id_width(width) {};

    void init(GlobalTestConfig *gtc) {
        std::cout << "initializing" << std::endl;
        // Persistent::init();
        // pds::init(gtc);
        uint64_t new_ops = total_ops / gtc->task_num;
        thd_ops = new uint64_t[gtc->task_num];
        for (auto i = 0; i<gtc->task_num; i++) {
            thd_ops[i] = new_ops;
        }
        if (new_ops * gtc->task_num != total_ops) {
            thd_ops[0] += (total_ops - new_ops * gtc->task_num);
        }

        Rideable* ptr = gtc->allocRideable();
        g = dynamic_cast<RGraph*>(ptr);
        if(!g){
            errexit("TGraphConstructionTest must be run on RGraph type object.");
        }

        // pds::init_thread(0); 
        /* set interval to inf so this won't be killed by timeout */
        gtc->interval = numeric_limits<double>::max();
        std::cout << "Finished init func" << std::endl;
    }
    int stream_edges_from_file(bool insert_edges, int num_threads, int tid) {
        for (int i = tid; i < num_files; i += num_threads) {
            std::stringstream ss;
            ss << std::setw(file_id_width) << std::setfill('0') << i;
            // Figure out which file is being opened...
            FILE *f = fopen((graphDir + base_fname + ss.str() + ".bin").c_str(), "r");
            struct stat buf;
            fstat(fileno(f), &buf);
            auto num_edges = buf.st_size / 8;
            int* a = new int[num_edges*2];
            size_t ret = fread(a, 8, num_edges*2, f);
            for (int j = 0; j < num_edges; j+=2) {
                if (insert_edges) {
                    g->add_edge(a[j], a[j+1], 1);
                } else if (! g->has_edge(a[j], a[j+1])) {
                    std::cout<<"verify failed on thread "<<tid<<std::endl;
                    return -1;
                }
            }
            fclose(f);
        }
        if (! insert_edges) std::cout<<"verify finished on thread "<<tid<<std::endl;
        return 0;
    }
    void parInit(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
        // pds::init_thread(ltc->tid);
        g->init_thread(gtc, ltc);
        // Loop through the files in parallel
        int num_threads = gtc->task_num;
        int tid = ltc->tid;
        if (tid == 0) {
            std::cout << "Started parinit" << std::endl;
        }
        // Allocate an array of edge structs, read the bytes from the file into this.
        // Open the file, take the size, and divide by 8 to get the number of edges in the file
        stream_edges_from_file(true, num_threads, tid);

        if (tid == 0) {
            std::cout << "Finished parinit" << std::endl;
        }
    }
    int execute(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
        if (ltc->tid == 0) {
            std::cout << "beginning execute" << std::endl;
        }
        auto num_threads = gtc->task_num;
        int tid = ltc->tid;
        
        stream_edges_from_file(false, num_threads, tid);

        if (ltc->tid == 0) {
            std::cout << "Time to initialize graph: " << gtc->parInit_time << " (ms)" << std::endl;
        }
        return 0;
    }
    
    void cleanup(GlobalTestConfig *gtc) {
        delete g;
    }
};
#endif
