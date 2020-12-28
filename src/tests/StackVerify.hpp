#ifndef STACKVERIFY_HPP
#define STACKVERIFY_HPP


#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "TestConfig.hpp"
#include "RStack.hpp"
#include <string> 
#include <vector>

template <typename T>
class StackVerify : public Test{

    public :
        thread_local int counter = 0;
        RStack<T>* s;
        std::vector<std::string> p; 
        RecoverVerifyTest(){};
        void init(GlobalTestConfig* gtc);
        void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
        int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
        void cleanup(GlobalTestConfig* gtc);

};


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
        std::string value = std::to_string(tid)+"_"+std::to_string(counter);
        s->push(value, tid);
        counter++;
    }
    else{
        std::string popped_val = s->pop(tid);
        p.push_back(popped_val);
    }
}

template <typename T>
int StackVerify<T>::execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    auto time_up = gtc->finish;
    
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
void StackVerify<T>::verify(){
    std::unordered_map<int, int> map; 
    for(std::string i : p){
        size_t pos = str.rfind("_"); 
        int tid = atoi(i.substr(0, pos));
        int monoval = atoi(i.substr(pos+1, i.length()-1));

        if (map.find(tid) == map.end()) {
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
void StackVerify<T>::cleanup(GlobalTestConfig* gtc){
    delete s;
}

#endif