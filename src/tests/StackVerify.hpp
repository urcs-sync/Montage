#ifndef STACKVERIFY_HPP
#define STACKVERIFY_HPP


#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "TestConfig.hpp"
#include "RStack.hpp"
#include <string> 
#include <utility>
#include <unordered_map>


class StackVerify : public Test{
    const int TID_SIZE=4;
    const int CNT_SIZE=60;
public:
    using T=std::string;
    int prop_push, prop_pop;
    static inline thread_local uint64_t counter = 0;
    RStack<T>* s;
    std::unordered_map<int, uint64_t> map; 
    StackVerify(int p_push, int p_pop) : prop_push(p_push), prop_pop(p_pop){}
    void operation(int op, int tid);
    void init(GlobalTestConfig* gtc);
    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
    void verify();
    void cleanup(GlobalTestConfig* gtc);

    inline T toString(int tid, uint64_t v){
        auto _tid = std::to_string(tid);
        assert(_tid.size()<=TID_SIZE);
        auto _v = std::to_string(v);
        assert(_v.size()<=CNT_SIZE);
        return std::string(TID_SIZE-_tid.size(),'0')+_tid+std::string(CNT_SIZE-_v.size(),'0')+_v;
    }
};

void StackVerify::parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    s->init_thread(gtc, ltc);
}


void StackVerify::init(GlobalTestConfig* gtc){
    if (gtc->task_num <= 1){
        errexit("StackVerify must run on multiple threads.");
    }

    Rideable* ptr = gtc->allocRideable();
    s = dynamic_cast<RStack<T>*>(ptr);
    if (!s) {
        errexit("StackVerify must be run on RStack<T> type object.");
    }
    if(gtc->verbose){
        printf("Pushes:%d Pops:%d\n",
        prop_push,100-prop_push);
    }

}

void StackVerify::operation(int op, int tid){
    if(op < this->prop_push){
        s->push(toString(tid,counter), tid);
        counter++;
    }
    else{
       s->pop(tid);
    }
}

int StackVerify::execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    auto time_up = gtc->finish;
    counter = 0;
    int ops = 0;
    uint64_t r = ltc->seed;
    std::mt19937_64 gen_p(r);

    int tid = ltc->tid;

    auto now = std::chrono::high_resolution_clock::now();

    while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){

        int p = abs((long)gen_p()%100);
        operation(p,tid);
        ops++;
        if (ops % 500 == 0){
            now = std::chrono::high_resolution_clock::now();
        }

    }
    return ops;
}

void StackVerify::verify(){
    // called only by main thread
    while(!s->is_empty()){
        auto popped_val = s->pop(0);
        if(popped_val.has_value()){
            int tid = std::stoi(popped_val.value().substr(0,TID_SIZE));
            uint64_t monoval = std::stoull(popped_val.value().substr(TID_SIZE));

            if (map.find(tid) != map.end()) {
                if(map[tid] < monoval){
                    std::cerr<<"tid: "<<tid<<"\n --- last value popped for this tid : "<<map[tid]<<"\n --- current value popped: "<<monoval<<"\n --- not verfied."<<std::endl;
                    exit(1);
                } else {
                    map[tid] = monoval;
                }
            } else {
                map[tid] = monoval;
            }
        }
    }
    std::cout<<"Verified!"<<std::endl;
}

void StackVerify::cleanup(GlobalTestConfig* gtc){
    verify();
    delete s;
}

#endif