#ifndef SYNCTEST_HPP
#define SYNCTEST_HPP

#include "MapChurnTest.hpp"
#include "QueueChurnTest.hpp"
#include "Recoverable.hpp"

template <class K, class V>
class MapSyncTest: public MapChurnTest<K,V>{
public:
    Recoverable* rec;
    int fs;
    int ft;
    int sync_cnt = 0;
    int range;

    MapSyncTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range, int prefill):
        MapChurnTest<K,V>(p_gets, p_puts, p_inserts, p_removes, range, prefill), range(range){}
    MapSyncTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
        MapChurnTest<K,V>(p_gets, p_puts, p_inserts, p_removes, range),  range(range){}
    
    void init(GlobalTestConfig* gtc){
        MapChurnTest<K,V>::init(gtc);
        rec = dynamic_cast<Recoverable*>(this->getRideable());
        if (!rec){
            errexit("MapSyncTest must be run on Recoverable type object.");
        }
        if (gtc->checkEnv("SyncFreq")){
            fs = stoi(gtc->getEnv("SyncFreq"));
        } else {
            gtc->setEnv("SyncFreq", "5");
            fs = 5;
        }
        if (gtc->checkEnv("TimedSyncFreq")){
            ft = stoi(gtc->getEnv("TimedSyncFreq"));
        } else {
            ft = 0;
        }
    }
    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        MapChurnTest<K,V>::parInit(gtc, ltc);
    }
    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        auto time_up = gtc->finish;
	
        int ops = 0;
        uint64_t r = ltc->seed;
        std::mt19937_64 gen_k(r);
        std::mt19937_64 gen_p(r+1);
        std::mt19937_64 gen_s(r+2);

        int tid = ltc->tid;

        int sync_latency_sum = 0;
        int sync_latency_test_cnt = 0;

        // atomic_thread_fence(std::memory_order_acq_rel);
        //broker->threadInit(gtc,ltc);
        auto now = std::chrono::high_resolution_clock::now();

        while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){

            r = abs((long)gen_k()%range);
            // r = abs(rand_nums[(k_idx++)%1000]%range);
            int p = abs((long)gen_p()%100);
            // int p = abs(rand_nums[(p_idx++)%1000]%100);
            
            this->operation(r, p, tid);

            
            if (fs != 0 && abs((long)gen_s())%fs == 0){
                if (ft && tid == 0 && sync_cnt % ft == 0){
                    auto before = std::chrono::high_resolution_clock::now();
                    rec->sync();
                    // std::cout<<"sync() latency:"<<
                    //     std::chrono::duration_cast<std::chrono::microseconds>(
                    //         chrono::high_resolution_clock::now()-before).count()
                    //     << "us"<<std::endl;
                    sync_latency_sum += std::chrono::duration_cast<std::chrono::microseconds>(
                            chrono::high_resolution_clock::now()-before).count();
                    sync_latency_test_cnt++;
                } else {
                    rec->sync();
                }
                sync_cnt++;
            }

            ops++;
            if (ops % 512 == 0){
                now = std::chrono::high_resolution_clock::now();
            }
            // TODO: replace this with __rdtsc
            // or use hrtimer (high-resolution timer API in linux.)
        }
        if (ft && tid == 0){
            std::cout<<"sync() latency:"<<(double)sync_latency_sum/sync_latency_test_cnt<<
            "us. sample count:"<<sync_latency_test_cnt<<std::endl;
        }
        // printf("thread %d returning at %f.3",
        //     ltc->tid, 
        //     chrono::duration_cast<chrono::microseconds>(
        //         chrono::high_resolution_clock::now()-gtc->start).count()/1000000.0);
        return ops;
    }
};

class QueueSyncTest: public QueueChurnTest{
public:
    Recoverable* rec;
    int fs;
    int ft;
    int sync_cnt = 0;

    QueueSyncTest(int p_enqs, int p_deqs, int prefill):
        QueueChurnTest(p_enqs, p_deqs, prefill){}
    
    void init(GlobalTestConfig* gtc){
        QueueChurnTest::init(gtc);
        rec = dynamic_cast<Recoverable*>(this->getRideable());
        if (!rec){
            errexit("QueueSyncTest must be run on Recoverable type object.");
        }
        if (gtc->checkEnv("SyncFreq")){
            fs = stoi(gtc->getEnv("SyncFreq"));
        } else {
            gtc->setEnv("SyncFreq", "5");
            fs = 5;
        }
        if (gtc->checkEnv("TimedSyncFreq")){
            ft = stoi(gtc->getEnv("TimedSyncFreq"));
        } else {
            ft = 0;
        }
    }
    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        QueueChurnTest::parInit(gtc, ltc);
    }
    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        auto time_up = gtc->finish;
	
        int ops = 0;
        uint64_t r = ltc->seed;
        std::mt19937_64 gen_p(r+1);
        std::mt19937_64 gen_s(r+2);

        int tid = ltc->tid;

        int sync_latency_sum = 0;
        int sync_latency_test_cnt = 0;

        // atomic_thread_fence(std::memory_order_acq_rel);
        //broker->threadInit(gtc,ltc);
        auto now = std::chrono::high_resolution_clock::now();

        while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){

            int p = abs((long)gen_p()%100);
            // int p = abs(rand_nums[(p_idx++)%1000]%100);
            
            this->operation(p, tid);

            
            if (fs != 0 && abs((long)gen_s())%fs == 0){
                if (ft && tid == 0 && sync_cnt % ft == 0){
                    
                    auto before = std::chrono::high_resolution_clock::now();
                    rec->sync();
                    // std::cout<<"sync() latency:"<<
                    //     std::chrono::duration_cast<std::chrono::microseconds>(
                    //         chrono::high_resolution_clock::now()-before).count()
                    //     << "us"<<std::endl;
                    sync_latency_sum += std::chrono::duration_cast<std::chrono::microseconds>(
                            chrono::high_resolution_clock::now()-before).count();
                    sync_latency_test_cnt++;
                        
                } else {
                    rec->sync();
                }
                sync_cnt++;
            }

            ops++;
            if (ops % 512 == 0){
                now = std::chrono::high_resolution_clock::now();
            }
            // TODO: replace this with __rdtsc
            // or use hrtimer (high-resolution timer API in linux.)
        }
        if (ft && tid == 0){
            std::cout<<"sync() latency:"<<(double)sync_latency_sum/sync_latency_test_cnt<<
            "us. sample count:"<<sync_latency_test_cnt<<std::endl;
        }
        // printf("thread %d returning at %f.3",
        //     ltc->tid, 
        //     chrono::duration_cast<chrono::microseconds>(
        //         chrono::high_resolution_clock::now()-gtc->start).count()/1000000.0);
        return ops;
    }
};

#endif