#ifndef TRACKER_HPP
#define TRACKER_HPP

#include "concurrentprimitives.hpp"
#include "testconfig.hpp"
#include <list>

class TestConfig;

class Trackable{
public:
	virtual uint64_t get_ts() = 0;
	virtual ~Trackable(){}
};

class Tracker{
private:
	int freq;
	bool collect;

	std::atomic<uint64_t> curr_complete;
	paddedAtomic<uint64_t>* upper_reservs;
	paddedAtomic<uint64_t>* lower_reservs;
	padded<uint64_t>* retire_counters;
	padded<std::list<Trackable*>>* retired;

	void empty(int tid);
	bool conflict(uint64_t* lower_epochs, uint64_t* upper_epochs, uint64_t birth_epoch);

public:
	int thread_cnt;
	Tracker(TestConfig* tc);
	void reserve_init(int tid);
	void reserve(uint64_t lower, uint64_t upper, int tid);
	void reserve_low(uint64_t lower, int tid);
	void reserve_up(uint64_t upper, int tid);
	void set_complete_ts(uint64_t complete_ts);

	void retire(Trackable* obj, int tid);
	void release(int tid);

	~Tracker(){}
};

#endif
