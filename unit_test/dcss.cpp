#include "Persistent.hpp"
#include "Recoverable.hpp"
#include "TestConfig.hpp"
#include "montage_global_api.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <pthread.h>
#include <cstdlib>

using namespace std;
using namespace pds;
namespace dcas{
    const int THREAD_NUM = 1;
    const int CNT_UPPER = 100000;

    atomic_nbptr_t<uint64_t> d;
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
            auto x = d.load();
            if(x.get_val<uint64_t>()>=CNT_UPPER) {
                break;
            }
            BEGIN_OP();
            if(d.CAS(x,x.get_val<uint64_t>()+1)) 
                real.fetch_add(1);
            END_OP;
        }
    }
    void increment_verify(size_t tid){
        pds::init_thread(tid);
        barrier();
        while(true){
            try{
                auto x = d.load();
                if(x.get_val<uint64_t>()>=CNT_UPPER) {
                    break;
                }
                BEGIN_OP_AUTOEND();
                if(d.CAS_verify(x,x.get_val<uint64_t>()+1)) 
                    real.fetch_add(1);
            } catch(EpochVerifyException& e){
                continue;
            }
        }
    }
}
// namespace pds{
// extern std::atomic<size_t> abort_cnt;
// extern std::atomic<size_t> total_cnt;
// }
int main(){
    GlobalTestConfig gtc;
    gtc.task_num=dcas::THREAD_NUM;
    // init Persistent allocator
    Persistent::init();
    // init epoch system
    pds::init(&gtc);
    vector<thread> thds;
    dcas::initSynchronizationPrimitives(dcas::THREAD_NUM);
    for(int i=0;i<dcas::THREAD_NUM;i++){
        if(i%2)
            thds.emplace_back(dcas::increment,i);
        else
            thds.emplace_back(dcas::increment_verify,i);
    }
    for(int i=0;i<dcas::THREAD_NUM;i++){
        thds[i].join();
    }
    cout<<"d = "<<dcas::d.load_val()<<endl<<"real = "<<dcas::real.load()<<endl;
    // cout<<"total cas: "<<pds::total_cnt.load()<<endl<<"abort cas: "<<pds::abort_cnt.load()<<endl;
    return 0;
}