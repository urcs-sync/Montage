#ifndef EPOCHSYSVERIFYTEST_HPP
#define EPOCHSYSVERIFYTEST_HPP

#include "TestConfig.hpp"
#include "EpochSys.hpp"
#include "Persistent.hpp"
#include "persist_struct_api.hpp"

using namespace pds;
class Foo : public PBlk{
public:
    void persist() {}
    int bar;
    Foo(int b): bar(b){}
};

class EpochSysVerifyTest : public Test{
public:
    EpochSys* esys;
    void init(GlobalTestConfig* gtc);
	void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc);
	int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc);
	void cleanup(GlobalTestConfig* gtc);

    EpochSysVerifyTest();
    void test(int line, bool r, string hint);
};

EpochSysVerifyTest::EpochSysVerifyTest(){
    
}

void EpochSysVerifyTest::init(GlobalTestConfig* gtc){
    Persistent::init();
    if (gtc->verbose){
        std::cout<<"EpochSysVerifyTest is currently ignoring rideables."<<std::endl;
    }
    if (gtc->task_num != 1){
        errexit("EpochSysVerify currently runs on only 1 thread.");
    }
    pds::init(gtc);
    // esys = new EpochSys();
    esys = pds::esys;
}

void EpochSysVerifyTest::parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    pds::init_thread(ltc->tid);
}

void EpochSysVerifyTest::test(int line, bool r, string hint){
    std::cout<<"line:"<<line<<":\t";
    if (r){
        std::cout<< "passed" <<std::endl;
    } else {
        std::cout<< "failed. hint: "<<hint<<std::endl;
    }
}
#define TEST(r, h) test(__LINE__, r, std::to_string(h))

int EpochSysVerifyTest::execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    // Hs: implement this as needed -- test directly on epoch system.
    
    // assert(ltc->tid == 0);
    // // So let's test the epoch system on a single thread:
    // {
    //     // basic transaction operations:
    //     esys->reset();
    //     uint64_t e1 = esys->begin_transaction();
    //     // this begin_transaction should return 0.
    //     TEST(e1 == 0, e1);
    //     uint64_t e2 = esys->begin_transaction();
    //     // this begin_transaction should also return 0.
    //     TEST(e2 == 0, e2);
    //     // active_transaction of epoch 0 should have 2 in it.
    //     TEST(esys->active_transactions[0].ui.load() == 2, esys->active_transactions[0].ui.load());
    //     esys->end_transaction(e1);
    //     esys->end_transaction(e2);
    //     // active_transaction of epoch 0 should have 0 in it.
    //     TEST(esys->active_transactions[0].ui.load() == 0, esys->active_transactions[0].ui.load());
    //     esys->reset();
    // };
    // {
    //     esys->reset();
    //     // pblk operations and epoch advance:
    //     esys->advance_epoch(0);
    //     TEST(esys->global_epoch->load() == 1, esys->global_epoch->load());
    //     Foo* foo = new Foo(1);
    //     uint64_t e1 = esys->begin_transaction();
    //     foo = esys->register_alloc_pblk(foo, e1);
    //     // to_be_persisted of epoch e1 (1) should have size 1.
    //     TEST(esys->to_be_persisted[e1%4].size() == 1, esys->to_be_persisted[e1%4].size());
    //     // the epoch of foo should be e1.
    //     TEST(foo->epoch == e1, foo->epoch);
    //     Foo* bar = esys->openwrite_pblk(foo, e1);
    //     esys->register_update_pblk(bar, e1);
    //     // openwrite_pblk should return the same copy.
    //     TEST(bar == foo, (uint64_t)foo);
    //     esys->advance_epoch(0);
    //     // epoch should remain the same, as we're assuming epoch is still 0 when we call advance_wpoch.
    //     TEST(esys->global_epoch->load() == e1, esys->global_epoch->load());

    //     esys->advance_epoch(e1);
    //     // epoch should be 2, as the epoch used to be e1 (1).
    //     TEST(esys->global_epoch->load() == e1+1, esys->global_epoch->load());
    //     // begin a new transaction.
    //     uint64_t e2 = esys->begin_transaction();
    //     // e2 should be 2.
    //     TEST(e2 == 2, e2);
    //     foo = esys->openwrite_pblk(bar, e2);
    //     esys->register_update_pblk(foo, e2);
    //     // now foo should be different from bar, since it's re-opened in e2.
    //     TEST(foo != bar, (uint64_t)foo);
    //     // bar should be put in e2's to-be-freed list.
    //     TEST(esys->to_be_freed[e2%4].size() == 1, esys->to_be_freed[e2%4].size());
    //     // foo should be put in e2's to-be-persisted list.
    //     TEST(esys->to_be_persisted[e2%4].size() == 1, esys->to_be_persisted[e2%4].size());
    //     // the following openwrite should throw an exception, since it's already e2.
    //     bool throwed = false;
    //     try{
    //         foo = esys->openwrite_pblk(foo, e1);
    //     } catch (OldSeeNewException& e){
    //         throwed = true;
    //     }
    //     TEST(throwed, e1);
    //     esys->end_transaction(e2);
    //     esys->end_transaction(e1);

    //     esys->advance_epoch(e2);
    //     // now epoch should be 3. e1's persist list should be empty.
    //     TEST(esys->to_be_persisted[e1%4].size() == 0, esys->to_be_persisted[e1%4].size());
    //     esys->advance_epoch(e2+1);
    //     // now epoch should be 3. e2's persist list should be empty.
    //     TEST(esys->to_be_persisted[e2%4].size() == 0, esys->to_be_persisted[e2%4].size());
    //     // e2's free list should not be empty.
    //     TEST(esys->to_be_freed[e2%4].size() != 0, esys->to_be_freed[e2%4].size());
    //     esys->advance_epoch(e2+2);
    //     // now epoch should be 4. e2's free list should be empty.
    //     TEST(esys->to_be_freed[e2%4].size() == 0, esys->to_be_freed[e2%4].size());

    //     esys->reset();
    // };

    return 0;
}

void EpochSysVerifyTest::cleanup(GlobalTestConfig* gtc){

}



#endif