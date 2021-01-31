#include "tracker.hpp"

Tracker::Tracker(TestConfig* tc){
	thread_cnt = tc->thread_cnt;
	curr_complete.store(0, std::memory_order_release);
	freq = 1000;
	collect = true;
	retired = new padded<std::list<Trackable*>>[thread_cnt];
	upper_reservs = new paddedAtomic<uint64_t>[thread_cnt];
	lower_reservs = new paddedAtomic<uint64_t>[thread_cnt];
	for (int i = 0; i < thread_cnt; i++){
		upper_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
		lower_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
	}
	retire_counters = new padded<uint64_t>[thread_cnt];
}

// WARNING: reserve_init now is NOT ROBUST. reserve() or reserve_up()
// should be called afterwards ASAP, or we need to track the ts of tails
// and init both lower and upper in reserve_init().
void Tracker::reserve_init(int tid){
	reserve_low(curr_complete, std::memory_order_seq_cst);
}

void Tracker::reserve(uint64_t lower, uint64_t upper, int tid){
	reserve_low(lower, tid);
	reserve_up(upper, tid);
}

void Tracker::reserve_low(uint64_t lower, int tid){
	lower_reservs[tid].ui.store(lower, std::memory_order_seq_cst);
}

void Tracker::reserve_up(uint64_t upper, int tid){
	upper_reservs[tid].ui.store(upper, std::memory_order_seq_cst);
}

void Tracker::set_complete_ts(uint64_t complete_ts){
	curr_complete.store(complete_ts, std::memory_order_release);
}

void Tracker::retire(Trackable* obj, int tid){
	std::list<Trackable*>* myTrash = &(retired[tid].ui);
	myTrash->push_back(obj);
	if(collect && retire_counters[tid]%freq==0){
		empty(tid);
	}
	retire_counters[tid]=retire_counters[tid]+1;
}

void Tracker::release(int tid){
	upper_reservs[tid].ui.store(UINT64_MAX,std::memory_order_seq_cst);
	lower_reservs[tid].ui.store(UINT64_MAX,std::memory_order_seq_cst);
}

bool Tracker::conflict(uint64_t* lower_epochs, uint64_t* upper_epochs, uint64_t target_epoch){
	for (int i = 0; i < thread_cnt; i++){
		if (upper_epochs[i] >= target_epoch && lower_epochs[i] <= target_epoch){
			return true;
		}
	}
	return false;
}

void Tracker::empty(int tid){
	uint64_t upper_epochs_arr[thread_cnt];
	uint64_t lower_epochs_arr[thread_cnt];
	for (int i = 0; i < thread_cnt; i++){
		//sequence matters.
		lower_epochs_arr[i] = lower_reservs[i].ui.load(std::memory_order_acquire);
		upper_epochs_arr[i] = upper_reservs[i].ui.load(std::memory_order_acquire);
	}
	std::list<Trackable*>* myTrash = &(retired[tid].ui);
	for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end; ) {
		Trackable* res = *iterator;
		if(!conflict(lower_epochs_arr, upper_epochs_arr, res->get_ts())){
			if (collect) delete(res);
			iterator = myTrash->erase(iterator);
		}
		else{++iterator;}
	}
}
