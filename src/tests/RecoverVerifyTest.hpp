#ifndef RECOVERVERIFYTEST_HPP
#define RECOVERVERIFYTEST_HPP

/*
 * This is a test to verify correctness of mappings' recovery.
 */

#include <unordered_map>
#include "TestConfig.hpp"
#include "AllocatorMacro.hpp"
#include "Persistent.hpp"
#include "persist_struct_api.hpp"
#include "Recoverable.hpp"

template <class K, class V>
class RecoverVerifyTest : public Test{
public:
    RMap<K,V>* m;
    Recoverable* rec;
    size_t ins_cnt = 1000000;
    size_t range = ins_cnt*10;
    size_t key_size = TESTS_KEY_SIZE;
    size_t val_size = TESTS_VAL_SIZE;
	std::string value_buffer; // for string kv only
    RecoverVerifyTest(){}
    void init(GlobalTestConfig* gtc);
    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
    void cleanup(GlobalTestConfig* gtc);

    inline K fromInt(uint64_t v);
};

template <class K, class V>
void RecoverVerifyTest<K,V>::parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    m->init_thread(gtc, ltc);
    // pds::init_thread(ltc->tid);
}

template <class K, class V>
void RecoverVerifyTest<K,V>::init(GlobalTestConfig* gtc){
    if (gtc->task_num != 1){
        errexit("RecoverVerifyTest only runs on single thread.");
    }
    // // init Persistent allocator
    // Persistent::init();

    // // init epoch system
    // pds::init(gtc);

    Rideable* ptr = gtc->allocRideable();
    m = dynamic_cast<RMap<K,V>*>(ptr);
    if (!m) {
        errexit("RecoverVerifyTest must be run on RMap<K,V> type object.");
    }
    rec = dynamic_cast<Recoverable*>(ptr);
    if (!rec){
        errexit("RecoverVerifyTest must be run on Recoverable type object.");
    }
    if (gtc->checkEnv("InsCnt")){
        ins_cnt = stoll(gtc->getEnv("InsCnt"));
        range = ins_cnt * 10;
    }

    /* set interval to inf so this won't be killed by timeout */
    gtc->interval = numeric_limits<double>::max();
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
            m->insert(k,value_buffer,tid);
            reference.try_emplace(k,value_buffer);
        } else {
            m->remove(k, tid);
            reference.erase(std::move(k));
        }

        ops++;
    }
    auto end = chrono::high_resolution_clock::now();
    auto dur = end - begin;
    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();

    std::cout<<"insert finished. Spent "<< dur_ms << "ms" <<std::endl;
    pds::flush();
    std::cout<<"epochsys flushed."<<std::endl;
    pds::esys->simulate_crash();
    std::cout<<"crashed."<<std::endl;
    int rec_cnt = rec->recover(true);
    std::cout<<"recover returned."<<std::endl;
    if (rec_cnt == (int)reference.size()){
        std::cout<<"rec_cnt currect."<<std::endl;
    } else {
        std::cout<<"recovered:"<<rec_cnt<<" expecting:"<<reference.size()<<std::endl;
        exit(1);
    }
    
    for (auto itr = reference.begin(); itr != reference.end(); itr++){
        if (!m->get(itr->first, tid)){
            std::cout<<"key:"<<itr->first<<"not recovered."<<std::endl;
            exit(1);
        }
    }
    std::cout<<"all records recovered."<<std::endl;
    return ops;
}

template <class K, class V>
void RecoverVerifyTest<K,V>::cleanup(GlobalTestConfig* gtc){
    delete m;
}

#endif
