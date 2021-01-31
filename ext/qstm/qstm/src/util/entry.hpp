#ifndef ENTRY_HPP
#define ENTRY_HPP


#include "bloomfilter.hpp"
#include "../config.hpp"
#include "writeset.hpp"
#include "freeset.hpp"
#include "concurrentprimitives.hpp"

enum class State { NotWriting, Writing, Complete };

class Entry{
public:
	unsigned long ts;
	filter wf;
	std::atomic<State> st;
};

#endif
