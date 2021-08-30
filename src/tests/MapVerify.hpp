#ifndef MAPVERIFY_HPP
#define MAPVERIFY_HPP

/*
 * This is a test that verifies mapping operations.
 * In multi-threaded scenarios, each thread operates on a cyclical partition of the key space.
 * E.g. for n threads, thread 0 does operations on key #0, #n, #2n etc.
 * This wouldn't catch all data races, but is good enough.
 */

#include "ChurnTest.hpp"
#include "TestConfig.hpp"
#include "RMap.hpp"
#include "optional.hpp"
#include <iostream>
#include <unordered_map>

//KEY_SIZE and VAL_SIZE are only for string kv
template <class K, class V>
class MapVerify : public ChurnTest{
public:
	RMap<K,V>* m;
	padded<std::unordered_map<K, V> *> *ground_truth_maps;
	int threads = 0;

	size_t key_size = TESTS_KEY_SIZE;
	size_t val_size = TESTS_VAL_SIZE;
	std::string value_buffer; // for string kv only
	MapVerify(int p_gets, int p_puts, int p_inserts, int p_removes, int range, int prefill):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range, prefill){}
	MapVerify(int p_gets, int p_puts, int p_inserts, int p_removes, int range):
		ChurnTest(p_gets, p_puts, p_inserts, p_removes, range){}

	inline K fromInt(uint64_t v);
	inline V getV(uint64_t v);

	virtual void init(GlobalTestConfig* gtc){
		#ifdef NDEBUG
		cout << "Error: MapVerify isn't allowed in release environments.\n";
		exit(1);
		#endif
		if(gtc->checkEnv("KeySize")){
			key_size = atoi((gtc->getEnv("KeySize")).c_str());
			assert(key_size<=TESTS_KEY_SIZE&&"KeySize dynamically passed in is greater than macro TESTS_KEY_SIZE!");
		}
		if(gtc->checkEnv("ValueSize")){
			val_size = atoi((gtc->getEnv("ValueSize")).c_str());
			assert(val_size<=TESTS_VAL_SIZE&&"ValueSize dynamically passed in is greater than macro TESTS_VAL_SIZE!");
		}
		threads = gtc->task_num;

		ground_truth_maps = new padded<std::unordered_map<K, V> *>[threads];
		for(int i = 0; i < threads; i++){
			ground_truth_maps[i].ui = new std::unordered_map<K, V>();
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
			 errexit("MapVerify must be run on RMap<K,V> type object.");
		}
	}
	Rideable* getRideable(){
		return m;
	}
	void doPrefill(GlobalTestConfig* gtc){
	// randomly prefill until specified amount of keys are successfully inserted
		if (this->prefill > 0){
			// We want each thread to get a roughly equal proportion
			// of inserts. As long as range >>> threads, we have that.
			assert(100 * threads < range);

			std::mt19937_64 gen_k(0);
			// int stride = this->range/this->prefill;
			int i = 0;
			while(i<this->prefill){
				auto ran = gen_k()%range;
				K k = this->fromInt(ran);
				m->insert(k,getV(ran),0);
				(*ground_truth_maps[ran % threads].ui)[k] = getV(ran);
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
	void operation(uint64_t key, int op, int tid);
	void cleanup(GlobalTestConfig* gtc){
		for(int tid=0;tid<threads;tid++){
			std::unordered_map<K, V> &my_map = (*ground_truth_maps[tid].ui);
			for(const auto& kv:my_map){
				optional<V> m_response = m->remove(kv.first,tid);
				if(!m_response.has_value() || m_response.value()!=kv.second){
					errexit("kv mismatch during cleanup!");
				}
			}
		}
		ChurnTest::cleanup(gtc);
#ifndef PRONTO
		// Pronto handles deletion by its own
		delete m;
#endif
		printf("Verified!\n");
	}
};

template <class K, class V>
inline K MapVerify<K,V>::fromInt(uint64_t v){
	return (K)v;
}

template<>
inline std::string MapVerify<std::string,std::string>::fromInt(uint64_t v){
	auto _key = std::to_string(v);
	return "user"+std::string(key_size-_key.size()-4,'0')+_key;
}

template <class K, class V>
inline V MapVerify<K,V>::getV(uint64_t v){
	return (V)v;
}

template<>
inline std::string MapVerify<std::string,std::string>::getV(uint64_t v){
	return value_buffer;
}

template<class K, class V>
inline void MapVerify<K,V>::operation(uint64_t key, int op, int tid){
	K k = this->fromInt(key * threads + tid);
	std::unordered_map<K, V> &my_map = (*ground_truth_maps[tid].ui);
	// printf("%d.\n", r);

	if(op<this->prop_gets){
		optional<V> m_response = m->get(k,tid);
		if(m_response.has_value()){
			auto ite = my_map.find(k);
			if(ite == my_map.end()){
				printf("MapVerify expected key in rideable: %s\n", k.c_str());
				exit(-1);
			}
			if(ite->second != *m_response){
				printf("MapVerify found incorrect value for key: %s\n", k.c_str());
				exit(-1);
			}
		}
	}
	else if(op<this->prop_puts){
		optional<V> m_response = m->put(k,getV(key),tid);
		auto ite = my_map.find(k);
		bool ground_contains_bool = (ite != my_map.end());
		if(m_response.has_value() != ground_contains_bool){
			printf("MapVerfiy response for put %d, expected %d for key: %s\n", m_response.has_value(), ground_contains_bool, k.c_str());
			exit(-1);
		}
		if(m_response.has_value() and *m_response != ite->second){
			printf("MapVerify response for put %s, expected %s for key: %s\n", m_response->c_str(), ite->second.c_str(), k.c_str());
			exit(-1);
		}
		my_map[k] = getV(key);
	}
	else if(op<this->prop_inserts){
		bool m_response = m->insert(k,getV(key),tid);
		bool m_expected = (my_map.find(k) == my_map.end());
		if(m_response != m_expected){
			printf("MapVerify response for insert %d, expected %d for key: %s\n", m_response, m_expected, k.c_str());
			exit(-1);
		}
		if(m_response)
			my_map[k] = getV(key);
	}
	else{ // op<=prop_removes
		optional<V> m_response = m->remove(k,tid);
		auto ite = my_map.find(k);
		bool ground_contains_bool = (ite != my_map.end());
		if(m_response.has_value() != ground_contains_bool){
			printf("MapVerfiy response for remove %d, expected %d for key: %s\n", m_response.has_value(), ground_contains_bool, k.c_str());
			exit(-1);
		}
		if(m_response.has_value() and *m_response != ite->second){
			printf("MapVerify response for remove %s, expected %s for key: %s\n", m_response->c_str(), ite->second.c_str(), k.c_str());
			exit(-1);
		}
		if(ground_contains_bool){
			my_map.erase(my_map.find(k));
		}
	}
}


#endif