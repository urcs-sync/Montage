// This test is obsolete
#if 0

#ifndef TOYTEST_HPP
#define TOYTEST_HPP

#include "TestConfig.hpp"
#include "Toy.hpp"
#include "PersistFunc.hpp"
#include "ConcurrentPrimitives.hpp"

class ToyTest : public Test{
    Toy* t;
    void init(GlobalTestConfig* gtc){
        // Rideable* r = gtc->allocRideable();
        // t = dynamic_cast<Toy*>(r);
        // if (!t){
        //     errexit("ToyTest must be run on Toy.");
        // }
        // pds::init(gtc);

        Persistent::init();
    }
	void cleanup(GlobalTestConfig* gtc){
        Persistent::finalize();
    }

	// called by all threads in parallel
	void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        // pds::init_thread(ltc->tid);
    }
	// runs the test
	// returns number of operations completed by that thread
	int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        // int tid = ltc->tid;
        // t->run(tid);
        // return 0;

        auto time_up = gtc->finish;

        int ops = 0;
        int size = 2<<8;

        padded<size_t>* payload = (padded<size_t>*)RP_malloc(sizeof(padded<size_t>) * size);
        payload = new (payload) padded<size_t>[size];

        auto now = std::chrono::high_resolution_clock::now();

        // std::cout<<"do nothing."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"flush the same cache line."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     persist_func::clwb(&payload[0].ui);
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write the same cache line."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     payload[0].ui = now.tv_usec;
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        std::cout<<"write-flush the same cache line."<<std::endl;
        while(std::chrono::duration_cast<std::chrono::microseconds>(time_up - now).count()>0){
            payload[0].ui = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()%1000000;// eqv to tv_usec
            persist_func::clwb(&payload[0].ui);
            ops++;
            if (ops % 512 == 0){
                now = std::chrono::high_resolution_clock::now();
            }
        }

        // std::cout<<"write-flush-write the same cache line."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     payload[0].ui = now.tv_usec;
        //     persist_func::clwb(&payload[0].ui);
        //     payload[0].ui = now.tv_usec+1;
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-flush-fence the same cache line."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     payload[0].ui = now.tv_usec;
        //     persist_func::clwb(&payload[0].ui);
        //     persist_func::sfence();
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"flush different cache line."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     persist_func::clwb(&payload[curr].ui);
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write different cache lines"<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     payload[curr].ui = now.tv_usec;
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-fence different cache lines."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     payload[curr].ui = now.tv_usec;
        //     persist_func::sfence();
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-flush different cache lines."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     payload[curr].ui = now.tv_usec;
        //     persist_func::clwb(&payload[curr].ui);
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-flush-write different cache lines."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     payload[curr].ui = now.tv_usec;
        //     persist_func::clwb(&payload[curr].ui);
        //     payload[curr].ui = now.tv_usec+1;
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-flush-fence different cache lines."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     payload[curr].ui = now.tv_usec;
        //     persist_func::clwb(&payload[curr].ui);
        //     persist_func::sfence();
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-flush-fence-write different cache lines."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     payload[curr].ui = now.tv_usec;
        //     persist_func::clwb(&payload[curr].ui);
        //     persist_func::sfence();
        //     payload[curr].ui = now.tv_usec+1;
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-flush-flush different cache lines."<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     payload[curr].ui = now.tv_usec;
        //     persist_func::clwb(&payload[curr].ui);
        //     persist_func::clwb(&payload[curr].ui);
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        // std::cout<<"write-flush*n different cache lines."<<std::endl;
        // int n = 1000;
        // std::cout<<"n="<<n<<std::endl;
        // while(timeDiff(&now,&time_up)>0){
        //     int curr = ops%(size);
        //     for (int i = 0; i < n; i++){
        //         payload[curr].ui = now.tv_usec+i;
        //         persist_func::clwb(&payload[curr].ui);
        //     }
        //     persist_func::sfence();
        //     ops++;
        //     if (ops % 512 == 0){
        //         gettimeofday(&now,NULL);
        //     }
        // }

        return ops;

    }
};



#endif

#endif