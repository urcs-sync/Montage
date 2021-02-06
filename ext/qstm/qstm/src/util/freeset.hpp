#ifndef FREESET
#define FREESET

#include "../config.hpp"

struct shared_fset{
};

struct free_record{
	void **addr;
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

class freeset{
public:
	unsigned int max;
	unsigned int size;
	void init();
	
	// void *lookup(void **addr);
	void add(void **addr);
	virtual void do_frees();
	bool empty();
	void clear();
	void flush();

	free_record* frees;
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
	freeset();
	virtual ~freeset();
};

#endif
