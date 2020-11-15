// This test is obsolete
#if 0

#ifndef KVTEST_HPP
#define KVTEST_HPP

#include "TestConfig.hpp"
#include "RMap.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <iostream>
#include <limits>
#include <cstdlib>

using namespace std;

class KVTest : public Test{
public:
    const std::string YCSB_PREFIX = "/localdisk2/ycsb_traces/ycsb/a-load-";
    RMap<std::string,std::string>* m;
    vector<std::string>** traces;
    std::string trace_prefix;
    std::string thd_num;
    size_t val_size = 1024;
    std::string value_buffer;
    KVTest(){
        //sz is 100k or 1m, wl is a or b
        trace_prefix = YCSB_PREFIX;
    }

    void getRideable(GlobalTestConfig* gtc){
        Rideable* ptr = gtc->allocRideable();
        m = dynamic_cast<RMap<std::string, std::string>*>(ptr);
        if (!m) {
             errexit("KVTest must be run on RMap<std::string,std::string> type object.");
        }
    }
    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        m->init_thread(gtc, ltc);
    }
    void init(GlobalTestConfig* gtc){

        if(gtc->checkEnv("ValueSize")){
            val_size = atoi((gtc->getEnv("ValueSize")).c_str());
        }
        value_buffer.reserve(val_size);
        for (size_t i = 0; i < val_size - 1; i++) {
            value_buffer += (char)((rand() % 2 == 0 ? 'A' : 'a') + (i % 26));
        }
        value_buffer+='\0';

        getRideable(gtc);

        thd_num = to_string(gtc->task_num);

        /* get workload */
        traces = new vector<std::string>* [gtc->task_num];
        std::string run_prefix = trace_prefix + thd_num + ".";
        for(int i=0;i<gtc->task_num;i++){
            traces[i] = new vector<std::string>();
            std::ifstream infile(run_prefix+to_string(i));
            std::string cmd;
            while(getline(infile, cmd)){
                traces[i]->push_back(cmd);
            }
        }

        /* set interval to inf so this won't be killed by timeout */
        gtc->interval = numeric_limits<double>::max();
    }
    void operation(const std::string& t, int tid){
        string tag = t.substr(0, 3);
        if (tag == "Add") {
            m->insert(t.substr(4), value_buffer, tid);
        } else {
            assert(0&&"invalid operation!");
        }
    }

    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        int tid = ltc->tid;
        int ops = 0;
        for (size_t i = 0; i < traces[tid]->size(); i++) {
            operation(traces[tid]->at(i), tid);
            ops++;
        }
        return ops;
    }
    void cleanup(GlobalTestConfig* gtc){
        delete m;
        for(int i=0;i<gtc->task_num;i++){
            delete traces[i];
        }
        delete traces;
    }
};

#endif

#endif