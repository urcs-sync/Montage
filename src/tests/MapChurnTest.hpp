#ifndef MAPCHURNTEST_HPP
#define MAPCHURNTEST_HPP

/*
 * This is a test with a time length for mappings.
 */

#include "ChurnTest.hpp"
#include "TestConfig.hpp"
#include "RMap.hpp"
#include <iostream>

//KEY_SIZE and VAL_SIZE are only for string kv
template <class K, class V>
class MapChurnTest : public ChurnTest{
public:
	RMap<K,V>* m;
	size_t key_size = TESTS_KEY_SIZE;
	size_t val_size = TESTS_VAL_SIZE;
	std::string value_buffer; // for string kv only
	MapChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range, int prefill):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range, prefill){}
	MapChurnTest(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range){}

	inline K fromInt(uint64_t v);

	virtual void init(GlobalTestConfig* gtc){
		if(gtc->checkEnv("KeySize")){
            key_size = atoi((gtc->getEnv("KeySize")).c_str());
			assert(key_size<=TESTS_KEY_SIZE&&"KeySize dynamically passed in is greater than macro TESTS_KEY_SIZE!");
        }
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

		ChurnTest::init(gtc);
	}

	virtual void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
		m->init_thread(gtc, ltc);
		ChurnTest::parInit(gtc, ltc);
	}

	void allocRideable(GlobalTestConfig* gtc){
		Rideable* ptr = gtc->allocRideable();
		m = dynamic_cast<RMap<K, V>*>(ptr);
		if (!m) {
			 errexit("MapChurnTest must be run on RMap<K,V> type object.");
		}
	}
	Rideable* getRideable(){
		return m;
	}
	void doPrefill(GlobalTestConfig* gtc){
		if (this->prefill > 0){
			/* Wentao: 
			 *	to avoid repeated k during prefilling, we instead 
			 *	insert [0,min(prefill-1,range)] 
			 */
			// std::mt19937_64 gen_k(0);
			// int stride = this->range/this->prefill;
			int i = 0;
			while(i<this->prefill){
				K k = this->fromInt(i%range);
				m->insert(k,k,0);
				i++;
			}
			if(gtc->verbose){
				printf("Prefilled %d\n",i);
			}
			Recoverable* rec=dynamic_cast<Recoverable*>(m);
			if(rec){
				rec->sync();
			}
		}
	}
	void operation(uint64_t key, int op, int tid){
		K k = this->fromInt(key);
		V v = k;
		// printf("%d.\n", r);
		
		if(op<this->prop_gets){
			m->get(k,tid);
		}
		else if(op<this->prop_puts){
			m->put(k,v,tid);
		}
		else if(op<this->prop_inserts){
			m->insert(k,v,tid);
		}
		else{ // op<=prop_removes
			m->remove(k,tid);
		}
	}
	void cleanup(GlobalTestConfig* gtc){
		ChurnTest::cleanup(gtc);
#ifndef PRONTO
		// Pronto handles deletion by its own
		delete m;
#endif
	}
};

template <class K, class V>
inline K MapChurnTest<K,V>::fromInt(uint64_t v){
	return (K)v;
}

template<>
inline std::string MapChurnTest<std::string,std::string>::fromInt(uint64_t v){
	auto _key = std::to_string(v);
	return "user"+std::string(key_size-_key.size()-4,'0')+_key;
}

template<>
inline void MapChurnTest<std::string,std::string>::doPrefill(GlobalTestConfig* gtc){
	// randomly prefill until specified amount of keys are successfully inserted
	if (this->prefill > 0){
		std::mt19937_64 gen_k(0);
		// int stride = this->range/this->prefill;
		int i = 0;
		while(i<this->prefill){
			std::string k = this->fromInt(gen_k()%range);
			m->insert(k,value_buffer,0);
			i++;
		}
		if(gtc->verbose){
			printf("Prefilled %d\n",i);
		}
		Recoverable* rec=dynamic_cast<Recoverable*>(m);
		if(rec){
			rec->sync();
		}
	}
}

template<>
inline void MapChurnTest<std::string,std::string>::operation(uint64_t key, int op, int tid){
	std::string k = this->fromInt(key);
	// printf("%d.\n", r);
	
	if(op<this->prop_gets){
		m->get(k,tid);
	}
	else if(op<this->prop_puts){
		m->put(k,value_buffer,tid);
	}
	else if(op<this->prop_inserts){
		m->insert(k,value_buffer,tid);
	}
	else{ // op<=prop_removes
		m->remove(k,tid);
	}
}


#endif