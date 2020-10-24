#include "Persistent.hpp"
#include "persist_struct_api.hpp"
#include "LLSC.hpp"
#include "TestConfig.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <pthread.h>
#include <cstdlib>

using namespace std;
using namespace pds;
namespace llsc{
    const int THREAD_NUM = 10;
    const int CNT_UPPER = 100000;

    atomic_dword_t<uint64_t> d;
    atomic<uint64_t> real;
    pthread_barrier_t pthread_barrier;
    void barrier()
    {
        pthread_barrier_wait(&pthread_barrier);
    }
    void initSynchronizationPrimitives(int task_num){
        // create barrier
        pthread_barrier_init(&pthread_barrier, NULL, task_num);
    }
    void increment(size_t tid){
        pds::init_thread(tid);
        barrier();
        while(true){
            BEGIN_OP();
            auto x = d.load_linked();
            if(x>=CNT_UPPER) {
                END_OP;
                break;
            }
            if(d.store_conditional(x,x+1)) 
                real.fetch_add(1);
            END_OP;
        }
    }
}

int main(){
    GlobalTestConfig gtc;
    gtc.task_num=llsc::THREAD_NUM;
    // init Persistent allocator
    Persistent::init();
    // init epoch system
    pds::init(&gtc);
    vector<thread> thds;
    llsc::initSynchronizationPrimitives(llsc::THREAD_NUM);
    for(int i=0;i<llsc::THREAD_NUM;i++){
        thds.emplace_back(llsc::increment,i);
    }
    for(int i=0;i<llsc::THREAD_NUM;i++){
        thds[i].join();
    }
    cout<<"d = "<<llsc::d.load()<<endl<<"real = "<<llsc::real.load()<<endl;
    return 0;
}