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
        static thread_local int counter;
        RStack<T>* s;
        std::unordered_map<int, int> map; 
        StackVerify(){}
        void operation(int tid);
        void init(GlobalTestConfig* gtc);
        void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
        int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
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
    if (gtc->task_num == 1){
        errexit("StackVerify must run on multiple threads.");
    }

    Rideable* ptr = gtc->allocRideable();
    s = dynamic_cast<RStack<T>*>(ptr);
    if (!s) {
        errexit("StackVerify must be run on RStack<T> type object.");
    }

}

template <typename T>
void StackVerify<T>::operation(int tid){
    if(tid != 0){
        pair<int,int> value;
        value.first = tid;
        value.second = counter;
        s->push(value, tid);
        counter++;
    }
    else{
        auto popped_val = s->pop(tid).value();
        int tid = popped_val.first;
        int monoval = popped_val.second;

        if (map.find(tid) != map.end()) {
            if(map[tid] > monoval){
                std::cout<<"tid:"<<tid<<"map monoval:"<<map[tid]<<"current monoval:"<<monoval<<"not recovered."<<std::endl;
                exit(1);
            } else {
                map[tid] = monoval;
            }
        } else {
            map[tid] = monoval;
        }
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

        
        operation(tid);
        ops++;
        if (ops % 500 == 0){
            now = std::chrono::high_resolution_clock::now();
        }

    }
    return ops;
}

template <typename T>
void StackVerify<T>::cleanup(GlobalTestConfig* gtc){

    delete s;
}

#endif