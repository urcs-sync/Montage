#ifndef TOY_HPP
#define TOY_HPP

#include <mutex>
#include "TestConfig.hpp"


class Toy : public Rideable{
public:
    class Payload : public pds::PBlk{
        GENERATE_FIELD(int, key, Payload);
        GENERATE_FIELD(int, val, Payload);
    public:
        Payload(){}
        Payload(int x, int y): m_key(x), m_val(y){}
        Payload(const Payload& oth): pds::PBlk(oth), m_key(oth.m_key), m_val(oth.m_val){}
        void persist(){}
    };

    

    Toy(GlobalTestConfig* gtc){
        
    }
    void run(int tid){
        // Payload* p = PNEW(Payload, 1, 1);
        // BEGIN_OP(p, p);
        // END_OP;
    }

    void run_parallel(int tid){

    }
};

class ToyFactory : public RideableFactory{
    Rideable* build(GlobalTestConfig* gtc){
        return new Toy(gtc);
    }
};

#endif