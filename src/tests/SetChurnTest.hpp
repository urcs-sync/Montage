// This test is obsolete
#if 0 

#ifndef SETCHURNTEST_HPP
#define SETCHURNTEST_HPP

#include "ChurnTest.hpp"
#include "TestConfig.hpp"
#include "RSet.hpp"

template <class T>
class SetChurnTest : public ChurnTest{
public:
	RSet<T>* s;

	SetChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range, int prefill):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range, prefill){}
	SetChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range){}

	inline T fromInt(uint64_t v);

	void getRideable(GlobalTestConfig* gtc){
		Rideable* ptr = gtc->allocRideable();
		s = dynamic_cast<RSet<T>*>(ptr);
		if (!s) {
			 errexit("SetChurnTest must be run on RSet<T> type object.");
		}
	}
	void doPrefill(GlobalTestConfig* gtc){
		// pds::init_thread(0);
		// prefill deterministically:
		if (this->prefill > 0){
			/* Wentao: 
			 *	to avoid repeated k during prefilling, we 
			 *	insert [0,min(prefill-1,range)] 
			 */
			// int stride = this->range/this->prefill;
			int i = 0;
			while(i<this->prefill){
				K k = this->fromInt(i%range);
				m->insert(k,0);
				i++;
			}
			if(gtc->verbose){
				printf("Prefilled %d\n",i);
			}
		}
	}
	void operation(uint64_t key, int op, int tid){
		T k = this->fromInt(key);
		// printf("%d.\n", r);
		
		if(op<this->prop_gets){
			s->get(k,tid);
		}
		else if(op<this->prop_puts){
			s->put(k,tid);
		}
		else if(op<this->prop_inserts){
			s->insert(k,tid);
		}
		else{ // op<=prop_removes
			s->remove(k,tid);
		}
	}

};

template <class T>
inline T SetChurnTest<T>::fromInt(uint64_t v){
	return (T)v;
}

template<>
inline std::string SetChurnTest<std::string>::fromInt(uint64_t v){
	return std::to_string(v);
}

#endif // SETCHURNTEST_HPP
#endif // 0