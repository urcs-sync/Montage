#include <iostream>
#include <fstream>
#include <cstring>
#include "BenchmarkSets.hpp"
// Macros suck, but it's either TL2 or TinySTM or ESTM, we can't have all at the same time
#include "datastructures/hashmaps/QSTMResizableHashSet.hpp"
#define DATA_FILENAME "data/set-hash-1k-qstm-1w-r500.txt"

int main(void) {
    const std::string dataFilename {DATA_FILENAME};
//    vector<int> threadList = { 1, 2, 4, 6, 8, 12, 16, 24, 32, 40, 48, 64, 72 };
//    vector<int> threadList = { 1, 5, 10, 20, 30, 40, 50, 60, 70, 80 };  // For 2x20a
    vector<int> threadList = { 1, 4, 8, 12, 16, 20, 24, 32, 36, 40, 48, 62, 72, 80, 90 };  // For 2x20a
    // vector<int> threadList = { 1}; // For doing one core count at a time
//    vector<int> ratioList = { 1000, 500, 100, 10, 1, 0 };        // Permil ratio: 100%, 50%, 10%, 1%, 0.1%, 0%
//    vector<int> ratioList = { 1000 };
    vector<int> ratioList = { 500 };
//    vector<int> ratioList = { 100 };
//    vector<int> ratioList = { 0 };
    const int numElements = 500000;                                // Number of keys in the set
    const int numRuns = 3;
    const seconds testLength = 5s;
    const int EMAX_CLASS = 10;
    uint64_t results[EMAX_CLASS][threadList.size()][ratioList.size()];
    std::string cNames[EMAX_CLASS];
    int maxClass = 0;
    // Reset results
    std::memset(results, 0, sizeof(uint64_t)*EMAX_CLASS*threadList.size()*ratioList.size());

    double totalHours = (double)EMAX_CLASS*ratioList.size()*threadList.size()*testLength.count()*numRuns/(60.*60.);
    std::cout << "This benchmark is going to take at most " << totalHours << " hours to complete\n";

    for (unsigned ir = 0; ir < ratioList.size(); ir++) {
        auto ratio = ratioList[ir];
        for (unsigned it = 0; it < threadList.size(); it++) {
            auto nThreads = threadList[it];
            int ic = 0;
            BenchmarkSets bench(nThreads);
            std::cout << "\n----- Sets (Hashtable)   numElements=" << numElements << "   ratio=" << ratio/10. << "%   threads=" << nThreads << "   runs=" << numRuns << "   length=" << testLength.count() << "s -----\n";
            results[ic][it][ir] = bench.benchmark<QSTMResizableHashSet<uint64_t>,uint64_t>           (cNames[ic], ratio, testLength, numRuns, numElements, false);
            ic++;
            maxClass = ic;
        }
    }

    // Export tab-separated values to a file to be imported in gnuplot or excel
    ofstream dataFile;
    dataFile.open(dataFilename);
    dataFile << "Threads\t";
    // Printf class names and ratios for each column
    for (unsigned ir = 0; ir < ratioList.size(); ir++) {
        auto ratio = ratioList[ir];
        for (int ic = 0; ic < maxClass; ic++) dataFile << cNames[ic] << "-" << ratio/10. << "%"<< "\t";
    }
    dataFile << "\n";
    for (int it = 0; it < threadList.size(); it++) {
        dataFile << threadList[it] << "\t";
        for (unsigned ir = 0; ir < ratioList.size(); ir++) {
            for (int ic = 0; ic < maxClass; ic++) dataFile << results[ic][it][ir] << "\t";
        }
        dataFile << "\n";
    }
    dataFile.close();
    std::cout << "\nSuccessfuly saved results in " << dataFilename << "\n";

    return 0;
}
