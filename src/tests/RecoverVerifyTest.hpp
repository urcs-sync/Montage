#ifndef RECOVERVERIFYTEST_HPP
#define RECOVERVERIFYTEST_HPP

/*
 * This is a test to verify correctness of mappings' recovery.
 */

#include <unordered_map>
#include "TestConfig.hpp"
#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "Recoverable.hpp"

template <class K, class V>
class RecoverVerifyTest : public Test{
public:
    GlobalTestConfig* _gtc;
    RMap<K,V>* m;
    Recoverable* rec;
    size_t ins_cnt = 1000000;
    size_t range = ins_cnt*10;
    size_t key_size = TESTS_KEY_SIZE;
    size_t val_size = TESTS_VAL_SIZE;
    pthread_barrier_t sync_point;
    RecoverVerifyTest(GlobalTestConfig* gtc): _gtc(gtc){}
    void init(GlobalTestConfig* gtc);
    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
    void cleanup(GlobalTestConfig* gtc);

    inline K fromInt(uint64_t v);
    void prepareRideable();
};

template <class K, class V>
void RecoverVerifyTest<K,V>::parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    m->init_thread(gtc, ltc);
}

template <class K, class V>
void RecoverVerifyTest<K,V>::prepareRideable() {
    Rideable* ptr = _gtc->allocRideable();
    m = dynamic_cast<RMap<K,V>*>(ptr);
    if (!m) {
        errexit("RecoverVerifyTest must be run on RMap<K,V> type object.");
    }
    rec = dynamic_cast<Recoverable*>(ptr);
    if (!rec){
        errexit("RecoverVerifyTest must be run on Recoverable type object.");
    }
}

template <class K, class V>
void RecoverVerifyTest<K,V>::init(GlobalTestConfig* gtc){
    prepareRideable();
    if (gtc->checkEnv("InsCnt")){
        ins_cnt = stoll(gtc->getEnv("InsCnt"));
        range = ins_cnt * 10;
    }

    /* set interval to inf so this won't be killed by timeout */
    gtc->interval = numeric_limits<double>::max();
    pthread_barrier_init(&sync_point, NULL, gtc->task_num);
}

template <class K, class V>
inline K RecoverVerifyTest<K,V>::fromInt(uint64_t v){
    return (K)v;
}

template<>
inline std::string RecoverVerifyTest<std::string,std::string>::fromInt(uint64_t v){
    auto _key = std::to_string(v);
    return "user"+std::string(key_size-_key.size()-4,'0')+_key;
}

template <class K, class V>
int RecoverVerifyTest<K,V>::execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    std::string value_buffer; // for string kv only
    if (!gtc->checkEnv("NoVerify")){
        if (ltc->tid == 0){ // FIXME: workaround when we can't change the total thread count of ralloc.
            std::unordered_map<K,V> reference;
            size_t ops = 0;
            uint64_t r = ltc->seed;
            std::mt19937_64 gen_k(r);
            std::mt19937_64 gen_p(r+1);
            value_buffer.reserve(val_size);
            value_buffer.clear();
            std::mt19937_64 gen_v(7);
            for (size_t i = 0; i < val_size - 1; i++) {
                value_buffer += (char)((i % 2 == 0 ? 'A' : 'a') + (gen_v() % 26));
            }
            value_buffer += '\0';
            int tid = ltc->tid;
            auto begin = chrono::high_resolution_clock::now();
            while(ops <= ins_cnt){
                r = abs((long)(gen_k()%range));
                int p = abs((long)gen_p()%100);
                K k = fromInt(r);
                if (p < 50){
                    auto ret1 = m->insert(k,value_buffer,tid);
                    auto ret2 = reference.try_emplace(k,value_buffer);
                    assert(ret1==ret2.second);
                    if(ret1){
                        ops++;
                    }
                } else {
                    auto ret1 = m->remove(k, tid);
                    auto ret2 = reference.erase(std::move(k));
                    assert(ret1.has_value() == ret2);
                    if(ret1.has_value()){
                        ops--;
                    }
                }
            }
            auto end = chrono::high_resolution_clock::now();
            auto dur = end - begin;
            auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();

            std::cout<<"insert finished. Spent "<< dur_ms << "ms" <<std::endl;
            rec->flush();
            std::cout<<"epochsys flushed."<<std::endl;
            delete m;
            std::cout<<"crashed."<<std::endl;
            prepareRideable();
            std::cout<<"recover returned."<<std::endl;
            auto rec_cnt = rec->get_last_recovered_cnt();
            if (rec_cnt == reference.size()){
                std::cout<<"rec_cnt currect."<<std::endl;
            } else {
                std::cout<<"recovered:"<<rec_cnt<<" expecting:"<<reference.size()<<std::endl;
                std::cout<<"Test FAILED!"<<std::endl;
                exit(1);
            }
            
            for (auto itr = reference.begin(); itr != reference.end(); itr++){
                if (!m->get(itr->first, tid)){
                    std::cout<<"key:"<<itr->first<<"not recovered."<<std::endl;
                    exit(1);
                }
            }
            std::cout<<"all records recovered."<<std::endl;
            std::cout<<"Test PASSED!"<<std::endl;
            return ops;
        } else {
            return 0;
        }
    } else {
        // we don't need to verify but just test the speed.
        // Multithreaded warmup
        size_t ops = 0;
        int tid = ltc->tid;
        size_t thd_ins_cnt = ins_cnt/gtc->task_num;
        if(tid==0) {
            thd_ins_cnt+=(ins_cnt-thd_ins_cnt*gtc->task_num);
        }
        uint64_t r = ltc->seed;
        std::mt19937_64 gen_k(r);
        std::mt19937_64 gen_p(r+1);
        value_buffer.reserve(val_size);
        value_buffer.clear();
        std::mt19937_64 gen_v(7);
        for (size_t i = 0; i < val_size - 1; i++) {
            value_buffer += (char)((i % 2 == 0 ? 'A' : 'a') + (gen_v() % 26));
        }
        value_buffer += '\0';
        pthread_barrier_wait(&sync_point);
        while(ops <= thd_ins_cnt){
            r = abs((long)(gen_k()%range));
            int p = abs((long)gen_p()%100);
            K k = fromInt(r);
            if (p < 50){
                auto ret1 = m->insert(k,value_buffer,tid);
                if(ret1){
                    ops++;
                }
            } else {
                auto ret1 = m->remove(k, tid);
                if(ret1.has_value()){
                    ops--;
                }
            }
        }
        pthread_barrier_wait(&sync_point);

        if(tid==0){
            rec->flush();
            std::cout<<"epochsys flushed."<<std::endl;
            delete m;
            std::cout<<"crashed."<<std::endl;
            prepareRideable();
            std::cout<<"recover returned."<<std::endl;
        }
        return ops;
    }
}

template <class K, class V>
void RecoverVerifyTest<K,V>::cleanup(GlobalTestConfig* gtc){
    delete m;
}

#endif
