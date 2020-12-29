#ifndef STACKVERIFY_HPP
#define STACKVERIFY_HPP


#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "TestConfig.hpp"
#include "RStack.hpp"
#include <string> 
#include <utility>
#include <unordered_map>


template <typename T>
class StackVerify : public Test{

    public :
        int prop_push, prop_pop;
        static thread_local int counter;
        RStack<T>* s;
        std::unordered_map<int, int> map; 
        StackVerify(int p_push, int p_pop) : prop_push(p_push), prop_pop(p_pop){}
        void operation(int op, int tid);
        void init(GlobalTestConfig* gtc);
        void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
        int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
        void verify(int tid);
        void cleanup(GlobalTestConfig* gtc);

};

template <typename T>
thread_local int StackVerify<T>::counter = 0;

template <typename T>
void StackVerify<T>::parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    s->init_thread(gtc, ltc);
}


template <typename T>
void StackVerify<T>::init(GlobalTestConfig* gtc){
    if (gtc->task_num <= 1){
        errexit("StackVerify must run on multiple threads.");
    }

    Rideable* ptr = gtc->allocRideable();
    s = dynamic_cast<RStack<pair<int, int>>*>(ptr);
    if (!s) {
        errexit("StackVerify must be run on RStack<T> type object.");
    }
    if(gtc->verbose){
        printf("Pushes:%d Pops:%d\n",
        prop_push,100-prop_push);
    }

}

template <typename T>
void StackVerify<T>::operation(int op, int tid){
    if(op < this->prop_push){
        pair<int,int> value;
        value.first = tid;
        value.second = counter;
        s->push(value, tid);
        counter++;
    }
    else{
       s->pop(tid);

    }
}

template <typename T>
int StackVerify<T>::execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
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

template <typename T>
void StackVerify<T>::verify(int tid){
    while(!s->is_empty()){
        auto popped_val = s->pop(tid);
        if(popped_val.has_value()){
            int tid = popped_val.value().first;
            int monoval = popped_val.value().second;

            if (map.find(tid) != map.end()) {
                if(map[tid] < monoval){
                    std::cout<<"tid: "<<tid<<"\n --- last value popped for this tid : "<<map[tid]<<"\n --- current value popped: "<<monoval<<"\n --- not verfied."<<std::endl;
                } else {
                    map[tid] = monoval;
                }
            } else {
                map[tid] = monoval;
            }

        }
    }
}

template <typename T>
void StackVerify<T>::cleanup(GlobalTestConfig* gtc){
    verify(0);

    delete s;
}

#endif