#ifndef STACKTEST_HPP
#define STACKTEST_HPP

/*
 * This is a test with a time length for stacks.
 */

#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "TestConfig.hpp"
#include "RStack.hpp"

class StackTest : public Test{
public:
    using V=std::string;
    int prop_push, prop_pop;
    int prefill = 2000;
    size_t val_size = TESTS_VAL_SIZE;
    std::string value_buffer; // for string kv only
    RStack<V>* s;

    StackTest(int p_push, int p_pop, int prefill){
        prop_push = p_push;
        prop_pop = prop_push + p_pop;

        this->prefill = prefill;
    }

    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        s->init_thread(gtc, ltc);
    }

    void init(GlobalTestConfig* gtc){
        if(gtc->checkEnv("ValueSize")){
            val_size = atoi((gtc->getEnv("ValueSize")).c_str());
            assert(val_size<=TESTS_VAL_SIZE&&"ValueSize dynamically passed in is greater than macro TESTS_VAL_SIZE!");
        }

        value_buffer.reserve(val_size);
        value_buffer.clear();
        std::mt19937_64 gen_v(7);
        for (size_t i = 0; i < val_size - 1; i++) {
            value_buffer += (char)((i % 2 == 0 ? 'A' : 'a') + (gen_v() % 26));
        }
        value_buffer += '\0';

        getRideable(gtc);
        
        if(gtc->verbose){
            printf("Pushes:%d Pops:%d\n",
            prop_push,100-prop_push);
        }
        
        // overrides for constructor arguments
        if(gtc->checkEnv("prefill")){
            prefill = atoi((gtc->getEnv("prefill")).c_str());
        }
        doPrefill(gtc);
    }

    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        auto time_up = gtc->finish;
        
        int ops = 0;
        uint64_t r = ltc->seed;
        // std::mt19937_64 gen_v(r);
        std::mt19937_64 gen_p(r);

        int tid = ltc->tid;

        // atomic_thread_fence(std::memory_order_acq_rel);
        auto now = std::chrono::high_resolution_clock::now();

        while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){

            int p = abs((long)gen_p()%100);
            // int p = abs(rand_nums[(p_idx++)%1000]%100);
            
            operation(p, tid);
            
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
        delete s;
    }
    void getRideable(GlobalTestConfig* gtc){
        Rideable* ptr = gtc->allocRideable();
        s = dynamic_cast<RStack<V>*>(ptr);
        if(!s){
            errexit("QueueChurnTest must be run on RStack<V> type object.");
        } 
    }
    void doPrefill(GlobalTestConfig* gtc){
        if (this->prefill > 0){
            int i = 0;
            while(i<this->prefill){
                s->push(value_buffer,0);
                i++;
            }
            if(gtc->verbose){
                printf("Prefilled %d\n",i);
            }
        }
    }

    void operation(int op, int tid){
        if(op < this->prop_push){
            s->push(value_buffer, tid);
        }
        else{// op<=prop_pop
            s->pop(tid);
        }
    }
};

#endif