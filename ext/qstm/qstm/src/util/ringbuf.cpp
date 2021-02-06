#include <atomic>
#include "ringbuf.hpp"
#include "bloomfilter.hpp"
#include "../config.hpp"

ringEntry::ringEntry(){
	//ts = prio = 0;
	ts = 0;
	st = State::Complete;
}

ringbuf::ringbuf(){
	ring_index.ui.store(0);
#ifdef DUR_LIN
	FLUSH(&ring);
	FLUSH(&ring_index.ui);
#endif
}

