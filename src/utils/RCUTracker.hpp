/*

Copyright 2017 University of Rochester

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 

*/



#ifndef RCU_TRACKER_HPP
#define RCU_TRACKER_HPP

#include <queue>
#include <list>
#include <vector>
#include <atomic>
#include "ConcurrentPrimitives.hpp"

#include "BaseTracker.hpp"


enum RCUType{type_RCU, type_QSBR};

class RCUTracker: public BaseTracker{
private:
	int task_num;
	int freq;
	int epochFreq;
	bool collect;
	RCUType type;
	
public:
	struct RCUInfo{
	public:
		void* obj;
		uint64_t epoch;
		std::function<void(void*)> destruct;
		RCUInfo(void* obj, uint64_t epoch, const std::function<void(void*)>& d):obj(obj), epoch(epoch), destruct(d){}
	};
	
private:
	paddedAtomic<uint64_t>* reservations;
	padded<uint64_t>* retire_counters;
	padded<uint64_t>* alloc_counters;
	padded<std::list<RCUInfo>>* retired; 
	// std::list<RCUInfo>* retired;

	std::atomic<uint64_t> epoch;

public:
	~RCUTracker(){};
	RCUTracker(int task_num, int epochFreq, int emptyFreq, RCUType type, bool collect): 
	 BaseTracker(task_num),task_num(task_num),freq(emptyFreq),epochFreq(epochFreq),collect(collect),type(type){
		retired = new padded<std::list<RCUTracker::RCUInfo>>[task_num];
		// retired = new std::list<RCUTracker::RCUInfo>[task_num];
		reservations = new paddedAtomic<uint64_t>[task_num];
		retire_counters = new padded<uint64_t>[task_num];
		// alloc_counters = new padded<uint64_t>[task_num];
		for (int i = 0; i<task_num; i++){
			reservations[i].ui.store(UINT64_MAX,std::memory_order_release);
			retired[i].ui.clear();
			// retired[i].clear();
		}
		epoch.store(0,std::memory_order_release);
	}
	RCUTracker(int task_num, int epochFreq, int emptyFreq) : RCUTracker(task_num,epochFreq,emptyFreq,type_RCU,true){}
	RCUTracker(int task_num, int epochFreq, int emptyFreq, bool collect) : 
		RCUTracker(task_num,epochFreq,emptyFreq,type_RCU,collect){}

	void __attribute__ ((deprecated)) reserve(uint64_t e, int tid){
		return start_op(tid);
	}
	
	// void* alloc(int tid){
	// 	alloc_counters[tid]=alloc_counters[tid]+1;
	// 	if(alloc_counters[tid]%(epochFreq*task_num)==0){
	// 		epoch.fetch_add(1,std::memory_order_acq_rel);
	// 	}
	// 	return (void*)malloc(sizeof(T));
	// }
	void start_op(int tid){
		if (type == type_RCU){
			uint64_t e = epoch.load(std::memory_order_acquire);
			reservations[tid].ui.store(e,std::memory_order_seq_cst);
		}
		
	}
	void end_op(int tid){
		if (type == type_RCU){
			reservations[tid].ui.store(UINT64_MAX,std::memory_order_seq_cst);
		} else { //if type == TYPE_QSBR
			uint64_t e = epoch.load(std::memory_order_acquire);
			reservations[tid].ui.store(e,std::memory_order_seq_cst);
		}
	}
	void reserve(int tid){
		start_op(tid);
	}
	void clear(int tid){
		end_op(tid);
	}



	inline void incrementEpoch(){
		epoch.fetch_add(1,std::memory_order_acq_rel);
	}
	
	// void __attribute__ ((deprecated)) retire(T* obj, uint64_t e, int tid){
	// 	return retire(obj,tid);
	// }
	
	// void retire(T* obj, int tid){
	// 	if(obj==NULL){return;}
	// 	std::list<RCUInfo>* myTrash = &(retired[tid].ui);
	// 	// for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
	// 	// 	assert(it->obj!=obj && "double retire error");
	// 	// }
			
	// 	uint64_t e = epoch.load(std::memory_order_acquire);
	// 	RCUInfo info = RCUInfo(obj,e);
	// 	myTrash->push_back(info);
	// 	if(collect && retire_counters[tid]%freq==0){
	// 		empty(tid);
	// 	}
	// 	retire_counters[tid]=retire_counters[tid]+1;
	// }
	void retire(void* obj, int tid, const std::function<void(void*)>& d){
		if(obj==NULL){return;}
		std::list<RCUInfo>* myTrash = &(retired[tid].ui);
		// std::list<RCUInfo>* myTrash = &(retired[tid]);

		// for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
		// 	assert(it->obj!=obj && "double retire error");
		// }
			
		uint64_t e = epoch.load(std::memory_order_acquire);
		RCUInfo info = RCUInfo(obj,e,d);
		myTrash->push_back(info);
		if(retire_counters[tid]%(epochFreq*task_num)==0){
			epoch.fetch_add(1,std::memory_order_acq_rel);
		}
		if(collect && retire_counters[tid]%freq==0){
			empty(tid);
		}
		retire_counters[tid]=retire_counters[tid]+1;
	}
	template<class T>
	void retire(T* obj, int tid){
		retire(obj, tid, [](void* o){
			delete(static_cast<T*>(o));
		});
	}
	
	void empty(int tid){
		uint64_t minEpoch = UINT64_MAX;
		for (int i = 0; i<task_num; i++){
			uint64_t res = reservations[i].ui.load(std::memory_order_seq_cst);
			if(res<minEpoch){
				minEpoch = res;
			}
		}
		
		// erase safe objects
		std::list<RCUInfo>* myTrash = &(retired[tid].ui);
		// std::list<RCUInfo>* myTrash = &(retired[tid]);

		for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end; ) {
			RCUInfo& res = *iterator;
			if(res.epoch<minEpoch){
				res.destruct(res.obj);
				iterator = myTrash->erase(iterator);
			}
			else{++iterator;}
		}
	}
		
	bool collecting(){return collect;}
	
};


#endif
