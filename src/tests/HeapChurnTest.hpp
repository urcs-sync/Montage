// This test is obsolete
#if 0 

#ifndef HEAPCHURNTEST_HPP
#define HEAPCHURNTEST_HPP

#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "TestConfig.hpp"
#include "HeapQueue.hpp"

template <class V>
class HeapChurnTest : public Test{
public:
    int prop_enqs, prop_deqs;
    int range;
    int prefill;
    HeapQueue<V,V>* q;

    HeapChurnTest(int p_enqs, int p_deqs, int range, int prefill){
        prop_enqs = p_enqs;
        prop_deqs = prop_enqs + p_deqs;

        this->range = range;
        this->prefill = prefill;
    }

    inline V fromInt(uint64_t v);

    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        q->init_thread(gtc, ltc);
        // pds::init_thread(ltc->tid);
    }

    void init(GlobalTestConfig* gtc){
        // // init Persistent allocator
        // Persistent::init();

        // // init epoch system
        // pds::init(gtc);

        getRideable(gtc);
        
        if(gtc->verbose){
            printf("Enqueues:%d Dequeues:%d\n",
            prop_enqs,100-prop_enqs);
        }
        
        // overrides for constructor arguments
        if(gtc->checkEnv("range")){
            range = atoi((gtc->getEnv("range")).c_str());
        }
        if(gtc->checkEnv("prefill")){
            prefill = atoi((gtc->getEnv("prefill")).c_str());
        }

        doPrefill(gtc);
        
    }

    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        auto time_up = gtc->finish;
        
        int ops = 0;
        uint64_t r = ltc->seed;
        std::mt19937_64 gen_v(r);
        std::mt19937_64 gen_p(r+1);

        int tid = ltc->tid;

        // atomic_thread_fence(std::memory_order_acq_rel);
        //broker->threadInit(gtc,ltc);
        auto now = std::chrono::high_resolution_clock::now();

        while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){

            r = abs((long)gen_v()%range);
            // r = abs(rand_nums[(k_idx++)%1000]%range);
            int p = abs((long)gen_p()%100);
            // int p = abs(rand_nums[(p_idx++)%1000]%100);
            
            operation(r, p, tid);
            
            ops++;
            if (ops % 500 == 0){
                now = std::chrono::high_resolution_clock::now();
            }
            // TODO: replace this with __rdtsc
            // or use hrtimer (high-resolution timer API in linux.)
        }
        return ops;
    }

    void cleanup(GlobalTestConfig* gtc){
        Persistent::finalize();
    }
    void getRideable(GlobalTestConfig* gtc){
        Rideable* ptr = gtc->allocRideable();
        q = dynamic_cast<HeapQueue<V,V>*>(ptr);
        if(!q){
            errexit("HeapChurnTest must be run on HeapQueue<V,V> type object.");
        } 
    }
    void doPrefill(GlobalTestConfig* gtc){
        if(this->prefill > 0){
            int stride = this->range/this->prefill;
            int i = 0;
            for(i = 0; i < this->prefill; i += stride){
                V k = this->fromInt(i);
                V v = this->fromInt(i);
                q->enqueue(k, v, 0);
            }
            if(gtc->verbose){
                printf("Prefilled %d\n", i);
            }
        }
    }

    void operation(uint64_t key, int op, int tid){
        if(op < this->prop_enqs){
            V k = this->fromInt(key);
            V v = this->fromInt(key);
            q->enqueue(k, v, tid);
        }
        else{// op<=prop_deqs
            q->dequeue(tid);
        }
    }
};

template<class V>
inline V HeapChurnTest<V>::fromInt(uint64_t v){
    return V(v);
}

template<>
inline std::string HeapChurnTest<std::string>::fromInt(uint64_t v){
    return std::to_string(v);
}

#endif

#endif // 0