/*
 * Copyright 2017-2018
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 *   Nachshon Cohen <nachshonc@gmail.com>
 *
 * This work is published under the MIT license. See LICENSE.txt
 */
#ifndef _BENCHMARK_SETS_H_
#define _BENCHMARK_SETS_H_

#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <assert.h>

#define pthread_create pthread_create

// For thread pinning
#include <hwloc.h>
#include "pinning.hpp"

using namespace std;
using namespace chrono;

// Regular UserData
struct UserData  {
    long long seq;
    int tid;
    UserData(long long lseq, int ltid=0) {
        this->seq = lseq;
        this->tid = ltid;
    }
    UserData() {
        this->seq = -2;
        this->tid = -2;
    }
    UserData(const UserData &other) : seq(other.seq), tid(other.tid) { }

    bool operator < (const UserData& other) const {
        return seq < other.seq;
    }
    bool operator == (const UserData& other) const {
        return seq == other.seq && tid == other.tid;
    }
    bool operator != (const UserData& other) const {
        return seq != other.seq || tid != other.tid;
    }
};

template<typename S, typename K>
struct argstruct {
	int updateRatio;
	long long *ops;
	int tid;
	atomic<bool> *startFlag;
	atomic<bool> *quit;
	int numElements;
	S* set;
	K** udarray;
	PinConfig *pinconf;
};

/**
 * An imprecise but fast random number generator
 */
uint64_t randomLong(uint64_t x) {
    x ^= x >> 12; // a
    x ^= x << 25; // b
    x ^= x >> 27; // c
    return x * 2685821657736338717LL;
}


namespace std {
    template <>
    struct hash<UserData> {
        std::size_t operator()(const UserData& k) const {
            using std::size_t;
            using std::hash;
            return (hash<long long>()(k.seq));  // This hash has no collisions, which is irealistic
        }
    };
}


//	auto rw_lambda = [this,&quit,&startFlag,&set,&udarray,&numElements](const int updateRatio, long long *ops, const int tid) {
//	auto rw_lambda = [this,&quit,&startFlag,&set,&udarray,&numElements](argstruct *foo) {
template<typename S, typename K>
inline void* lambdaspawn(void *_foo){
	argstruct<S,K> *foo = (argstruct<S,K>*)_foo;
	int updateRatio = foo->updateRatio;
	long long *ops = foo->ops;
	const int tid = foo->tid;
	atomic<bool> *startFlag = foo->startFlag;
	atomic<bool> *quit = foo->quit;
	int numElements = foo->numElements;
	auto set = foo->set;
	auto udarray = foo->udarray;
	PinConfig * pinconf = foo->pinconf;

    long long numOps = 0;
	while (!startFlag->load()) ; // spin
	uint64_t seed = tid+1234567890123456781ULL;

	hwloc_cpuset_t cpuset = pinconf->affinities[tid]->cpuset;
	hwloc_set_cpubind(pinconf->topology,cpuset,HWLOC_CPUBIND_THREAD);

	while (!quit->load()) {
	for(int i=0; i<5; i++){ // Minimize effect of checking quit flag
		seed = randomLong(seed);
		int update = seed%1000;
		seed = randomLong(seed);
		auto ix = (unsigned int)(seed%numElements);
		if (update < updateRatio) {
			// I'm a Writer
//			if (set->remove(*udarray[ix], tid)) {
//				numOps++;
//				set->add(*udarray[ix], tid);
//			}
			set->bigtxn(udarray, tid, numElements);
			numOps++;
		} else {
			// I'm a Reader
			set->contains(*udarray[ix], tid);
			seed = randomLong(seed);
			ix = (unsigned int)(seed%numElements);
			set->contains(*udarray[ix], tid);
			numOps += 2;
//			set->bigreadtxn(udarray, tid, numElements);
//			numOps++;
		}
	}
	}
	*ops = numOps;
	return NULL;
}


/**
 * This is a micro-benchmark of sets
 */
class BenchmarkSets {

private:
    struct Result {
        nanoseconds nsEnq = 0ns;
        nanoseconds nsDeq = 0ns;
        long long numEnq = 0;
        long long numDeq = 0;
        long long totOpsSec = 0;

        Result() { }

        Result(const Result &other) {
            nsEnq = other.nsEnq;
            nsDeq = other.nsDeq;
            numEnq = other.numEnq;
            numDeq = other.numDeq;
            totOpsSec = other.totOpsSec;
        }

        bool operator < (const Result& other) const {
            return totOpsSec < other.totOpsSec;
        }
    };

    static const long long NSEC_IN_SEC = 1000000000LL;

    int numThreads;

public:
    BenchmarkSets(int numThreads) {
        this->numThreads = numThreads;
    }


    /**
     * When doing "updates" we execute a random removal and if the removal is successful we do an add() of the
     * same item immediately after. This keeps the size of the data structure equal to the original size (minus
     * MAX_THREADS items at most) which gives more deterministic results.
     */
    template<typename S, typename K>
    long long benchmark(std::string& className, const int updateRatio, const seconds testLengthSeconds, const int numRuns, const int numElements, const bool dedicated=false) {
        long long ops[numThreads][numRuns];
        long long lengthSec[numRuns];
        atomic<bool> quit = { false };
        atomic<bool> startFlag = { false };

        className = S::className();
        std::cout << "##### " << S::className() << " #####  \n";
        S* set = new S(numThreads);
        // Create all the keys in the concurrent set
        K** udarray = new K*[numElements];
        for (int i = 0; i < numElements; i++) udarray[i] = new K(i);
        // Add all the items to the list
        set->addAll(udarray, numElements, 0);

	PinConfig *pinconf = new PinConfig();
	hwloc_topology_init(&(pinconf->topology));
	hwloc_topology_load(pinconf->topology);
	pinconf->num_procs = hwloc_get_nbobjs_by_depth(pinconf->topology,  
	 hwloc_get_type_depth(pinconf->topology, HWLOC_OBJ_PU));
	pinconf->buildAffinity();

        // Can either be a Reader or a Writer
        //auto rw_lambda = [this,&quit,&startFlag,&set,&udarray,&numElements](const int updateRatio, long long *ops, const int tid) {
/*        auto rw_lambda = [this,&quit,&startFlag,&set,&udarray,&numElements](argstruct *foo) {
			int updateRatio = foo->updateRatio;
			long long *ops = foo->ops;
			const int tid = foo->tid;

            long long numOps = 0;
            while (!startFlag.load()) ; // spin
            uint64_t seed = tid+1234567890123456781ULL;
            while (!quit.load()) {
                seed = randomLong(seed);
                int update = seed%1000;
                seed = randomLong(seed);
                auto ix = (unsigned int)(seed%numElements);
                if (update < updateRatio) {
                    // I'm a Writer
                    if (set->remove(*udarray[ix], tid)) {
                    	numOps++;
                    	set->add(*udarray[ix], tid);
                    }
                    numOps++;
                } else {
                	// I'm a Reader
                    set->contains(*udarray[ix], tid);
                    seed = randomLong(seed);
                    ix = (unsigned int)(seed%numElements);
                    set->contains(*udarray[ix], tid);
                    numOps += 2;
                }
            }
            *ops = numOps;
        };
*/
		argstruct<S,K> *foo[numThreads];

        for (int irun = 0; irun < numRuns; irun++) {
            //thread rwThreads[numThreads];
			int result_code;
			pthread_t threads[numThreads];
            if (dedicated) {
				assert(0);
               // rwThreads[0] = thread(rw_lambda, 1000, &ops[0][irun], 0);
               // rwThreads[1] = thread(rw_lambda, 1000, &ops[1][irun], 1);
               // for (int tid = 2; tid < numThreads; tid++) rwThreads[tid] = thread(rw_lambda, updateRatio, &ops[tid][irun], tid);
            } else {
                for (int tid = 0; tid < numThreads; tid++){
					foo[tid] = new argstruct<S,K>;
					foo[tid]->updateRatio = updateRatio;
					foo[tid]->ops = &ops[tid][irun];
					foo[tid]->tid = tid;
					foo[tid]->startFlag = &startFlag;
					foo[tid]->quit = &quit;
					foo[tid]->numElements = numElements;
					foo[tid]->set = set;
					foo[tid]->udarray = udarray;
					foo[tid]->pinconf = pinconf;
				//	rwThreads[tid] = thread(rw_lambda, updateRatio, &ops[tid][irun], tid);
                	result_code = pthread_create(&threads[tid], NULL, lambdaspawn<S,K>, foo[tid]);
				}
            }
            this_thread::sleep_for(100ms);
            auto startBeats = steady_clock::now();
            startFlag.store(true);
            // Sleep for testLengthSeconds seconds
            this_thread::sleep_for(testLengthSeconds);
            quit.store(true);
            auto stopBeats = steady_clock::now();
            //for (int tid = 0; tid < numThreads; tid++) rwThreads[tid].join();
            for (int tid = 0; tid < numThreads; tid++){
				pthread_join(threads[tid], NULL);
				delete(foo[tid]);
			}
            lengthSec[irun] = (stopBeats-startBeats).count();
            if (dedicated) {
                // We don't account for the write-only operations but we aggregate the values from the two threads and display them
                std::cout << "Mutative transactions per second = " << (ops[0][irun] + ops[1][irun])*1000000000LL/lengthSec[irun] << "\n";
                ops[0][irun] = 0;
                ops[1][irun] = 0;
            }
            quit.store(false);
            startFlag.store(false);
            // Compute ops at the end of each run
            long long agg = 0;
            for (int tid = 0; tid < numThreads; tid++) {
                agg += ops[tid][irun]*1000000000LL/lengthSec[irun];
            }
        }

        // Clear the set, one key at a time and then delete the instance
        for (int i = 0; i < numElements; i++) set->remove(*udarray[i], 0);
        delete set;

        for (int i = 0; i < numElements; i++) delete udarray[i];
        delete[] udarray;

        // Accounting
        vector<long long> agg(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            for (int tid = 0; tid < numThreads; tid++) {
                agg[irun] += ops[tid][irun]*1000000000LL/lengthSec[irun];
            }
        }

        // Compute the median. numRuns must be an odd number
        sort(agg.begin(),agg.end());
        auto maxops = agg[numRuns-1];
        auto minops = agg[0];
        auto medianops = agg[numRuns/2];
        auto delta = (long)(100.*(maxops-minops) / ((double)medianops));
        // Printed value is the median of the number of ops per second that all threads were able to accomplish (on average)
        std::cout << "Ops/sec = " << medianops << "      delta = " << delta << "%   min = " << minops << "   max = " << maxops << "\n";
        return medianops;
    }



    /*
     * Inspired by Trevor Brown's benchmarks (does everyone else do it like this?)
     */
    template<typename S, typename K>
    long long benchmarkRandomFill(std::string& className, const int updateRatio, const seconds testLengthSeconds, const int numRuns, const int numElements, const bool dedicated=false) {
        long long ops[numThreads][numRuns];
        long long lengthSec[numRuns];
        atomic<bool> quit = { false };
        atomic<bool> startFlag = { false };

        className = S::className();
        std::cout << "##### " << S::className() << " #####  \n";
        S* set = new S(numThreads);
        // Create all the keys in the concurrent set
        K** udarray = new K*[2*numElements];
        for (int i = 0; i < 2*numElements; i++) udarray[i] = new K(i);
        // Add half the keys to the list
        long ielem = 0;
        uint64_t seed = 1234567890123456781ULL;
        while (ielem < numElements/2) {
            seed = randomLong(seed);
            // Insert new random keys until we have 'numElements/2' keys in the tree
            if (set->add(*udarray[seed%(numElements)], 0)) ielem++;
        }
        // Add all keys, repeating if needed
        set->addAll(udarray, numElements, 0);

        // Can either be a Reader or a Writer
        //auto rw_lambda = [this,&quit,&startFlag,&set,&udarray,&numElements](const int updateRatio, long long *ops, const int tid) {
/*        auto rw_lambda = [this,&quit,&startFlag,&set,&udarray,&numElements](argstruct *foo) {
			int updateRatio = foo->updateRatio;
			long long *ops = foo->ops;
			const int tid = foo->tid;

            long long numOps = 0;
            while (!startFlag.load()) ; // spin
            uint64_t seed = tid+1234567890123456781ULL;
            while (!quit.load()) {
                seed = randomLong(seed);
                int update = seed%1000;
                seed = randomLong(seed);
                auto ix = (unsigned int)(seed%numElements);
                if (update < updateRatio) {
                    // I'm a Writer
                    if (set->remove(*udarray[ix], tid)) {
                        numOps++;
                        set->add(*udarray[ix], tid);
                    }
                    numOps++;
                } else {
                    // I'm a Reader
                    set->contains(*udarray[ix], tid);
                    seed = randomLong(seed);
                    ix = (unsigned int)(seed%numElements);
                    set->contains(*udarray[ix], tid);
                    numOps += 2;
                }
            }
            *ops = numOps;
        };
*/
		argstruct<S,K> *foo[numThreads];

        for (int irun = 0; irun < numRuns; irun++) {
            //thread rwThreads[numThreads];
			int result_code;
			pthread_t threads[numThreads];
            if (dedicated) {
				assert(0);
                //rwThreads[0] = thread(rw_lambda, 1000, &ops[0][irun], 0);
                //rwThreads[1] = thread(rw_lambda, 1000, &ops[1][irun], 1);
                //for (int tid = 2; tid < numThreads; tid++) rwThreads[tid] = thread(rw_lambda, updateRatio, &ops[tid][irun], tid);
            } else {
                //for (int tid = 0; tid < numThreads; tid++) rwThreads[tid] = thread(rw_lambda, updateRatio, &ops[tid][irun], tid);
                for (int tid = 0; tid < numThreads; tid++){
					foo[tid] = new argstruct<S,K>;
					foo[tid]->updateRatio = updateRatio;
					foo[tid]->ops = &ops[tid][irun];
					foo[tid]->tid = tid;
					foo[tid]->startFlag = &startFlag;
					foo[tid]->quit = &quit;
					foo[tid]->numElements = numElements;
					foo[tid]->set = set;
					foo[tid]->udarray = udarray;
					result_code = pthread_create(&threads[tid], NULL, lambdaspawn, foo[tid]);
				}
            }
            this_thread::sleep_for(100ms);
            auto startBeats = steady_clock::now();
            startFlag.store(true);
            // Sleep for testLengthSeconds seconds
            this_thread::sleep_for(testLengthSeconds);
            quit.store(true);
            auto stopBeats = steady_clock::now();
            //for (int tid = 0; tid < numThreads; tid++) rwThreads[tid].join();
            for (int tid = 0; tid < numThreads; tid++){
				pthread_join(threads[tid], NULL);
				delete(foo[tid]);
			}
            lengthSec[irun] = (stopBeats-startBeats).count();
            if (dedicated) {
                // We don't account for the write-only operations but we aggregate the values from the two threads and display them
                std::cout << "Mutative transactions per second = " << (ops[0][irun] + ops[1][irun])*1000000000LL/lengthSec[irun] << "\n";
                ops[0][irun] = 0;
                ops[1][irun] = 0;
            }
            quit.store(false);
            startFlag.store(false);
            // Compute ops at the end of each run
            long long agg = 0;
            for (int tid = 0; tid < numThreads; tid++) {
                agg += ops[tid][irun]*1000000000LL/lengthSec[irun];
            }
        }

        /* Clear the tree, one key at a time and then delete the instance */
        for (int i = 0; i < numElements; i++) set->remove(*udarray[i], 0);
        delete set;

        for (int i = 0; i < numElements; i++) delete udarray[i];
        delete[] udarray;

        // Accounting
        vector<long long> agg(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            for (int tid = 0; tid < numThreads; tid++) {
                agg[irun] += ops[tid][irun]*1000000000LL/lengthSec[irun];
            }
        }

        // Compute the median. numRuns must be an odd number
        sort(agg.begin(),agg.end());
        auto maxops = agg[numRuns-1];
        auto minops = agg[0];
        auto medianops = agg[numRuns/2];
        auto delta = (long)(100.*(maxops-minops) / ((double)medianops));
        // Printed value is the median of the number of ops per second that all threads were able to accomplish (on average)
        std::cout << "Ops/sec = " << medianops << "      delta = " << delta << "%   min = " << minops << "   max = " << maxops << "\n";
        return medianops;
    }

};

#endif
