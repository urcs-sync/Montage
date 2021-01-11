#ifndef ALLOC_TEST_HPP
#define ALLOC_TEST_HPP

// Simple allocation test for JEMalloc vs 

#include <cstdint>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <vector>
#include <cmath>
#include <ralloc.hpp>
#include "Recoverable.hpp"


enum AllocTestType {
    DO_JEMALLOC_ALLOC,
    DO_RALLOC_ALLOC,
    DO_MONTAGE_ALLOC
};

class DummyObject : public pds::PBlk {
    uint8_t data[64];
    void persist();
};

struct MontageDummy : public Recoverable {
    int recover(bool simulated) { return 0; }
    MontageDummy(GlobalTestConfig *gtc) : Recoverable(gtc) {}
    ~MontageDummy() {}

    DummyObject *create() { 
        MontageOpHolder op(this);
        auto ret = pnew<DummyObject>();
        return ret;    
    }

    void destroy(DummyObject *obj) { 
        MontageOpHolder op(this);    
        pdelete(obj); 
    }
};


class AllocTest : public Test {
    public:
        MontageDummy *dummy;
        uint64_t total_ops;
        uint64_t *thd_ops;
        enum AllocTestType allocType;

        AllocTest(uint64_t ops, enum AllocTestType allocType) : total_ops(ops), allocType(allocType) {}

        void init(GlobalTestConfig *gtc) {
            uint64_t new_ops = total_ops / gtc->task_num;
            thd_ops = new uint64_t[gtc->task_num];
            for (int i = 0; i<gtc->task_num; i++) {
                thd_ops[i] = new_ops;
            }
            if (new_ops * gtc->task_num != total_ops) {
                thd_ops[0] += (total_ops - new_ops * gtc->task_num);
            }
            dummy = new MontageDummy(gtc);
	
            if (allocType == DO_RALLOC_ALLOC) Persistent::init();	    
            /* set interval to inf so this won't be killed by timeout */
            gtc->interval = numeric_limits<double>::max();
        }

        int execute(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
            int tid = ltc->tid;
            std::vector<DummyObject*> objs;
            for (size_t i = 0; i < thd_ops[tid]; i++) {
                switch (allocType) {
                    case DO_JEMALLOC_ALLOC: {
                        objs.push_back(new DummyObject());
                        break;
                    }
                    case DO_RALLOC_ALLOC: {
                        objs.push_back((DummyObject*) RP_malloc(sizeof(DummyObject)));
			new (objs.back()) DummyObject();
                        break;
                    }
                    case DO_MONTAGE_ALLOC: {
                        objs.push_back(dummy->create());
                    }
                }
            }
            for (auto obj : objs) {
                switch (allocType) {
                    case DO_JEMALLOC_ALLOC: {
                        delete obj;
                        break;
                    }
                    case DO_RALLOC_ALLOC: {
                        RP_free(obj);
                        break;
                    }
                    case DO_MONTAGE_ALLOC: {
                        dummy->destroy(obj);
                    }
                }
            }
            return thd_ops[ltc->tid];
        }

        void cleanup(GlobalTestConfig *gtc) {
        }

        void parInit(GlobalTestConfig *gtc, LocalTestConfig *ltc) {
             Persistent::init_thread(ltc->tid);
	}
};
#endif
