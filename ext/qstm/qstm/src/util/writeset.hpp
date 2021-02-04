#ifndef WRITESET
#define WRITESET

#include "../config.hpp"

struct shared_wset{
};

struct write_record{
	void **addr;
	void *val;
	void *mask;
	void *operator new(size_t sz){
		return pstm_pmalloc(sz);
	}
	void *operator new[](size_t sz){
		return pstm_pmalloc(sz);
	}
	void operator delete(void *p){
		pstm_pfree(p);
	}
	void operator delete[](void *p){
		pstm_pfree(p);
	}
};

class writeset{
public:
	unsigned int max;
	unsigned int size;
	void init();
	
	// void *lookup(void **addr);
	bool lookup(void **addr, void** ret);
	void add(void **addr, void *val, void *mask);
	virtual void do_writes();
	bool empty();
	void clear();
	void flush();

	write_record* writes;
	void *operator new(size_t sz){
		return pstm_pmalloc(sz);
	}
	void *operator new[](size_t sz){
		return pstm_pmalloc(sz);
	}
	void operator delete(void *p){
		pstm_pfree(p);
	}
	void operator delete[](void *p){
		pstm_pfree(p);
	}
	writeset();
	virtual ~writeset();
};

#endif
