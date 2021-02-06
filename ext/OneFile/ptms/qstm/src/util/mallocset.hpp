#ifndef MALLOCSET
#define MALLOCSET

#include "../../config.hpp"

struct shared_mset{
};

struct malloc_record{
	void **addr;
	void *operator new(size_t sz){
		return pstm_pmalloc(sz);
	}
	void* operator new[](size_t sz){
		return pstm_pmalloc(sz);
	}
	void operator delete(void *p){
		pstm_pfree(p);
	}
	void operator delete[](void* p){
		pstm_pfree(p);
	}
};

class mallocset{
public:
	unsigned int max;
	unsigned int size;
	void init();
	
	// void *lookup(void **addr);
	void add(void **addr);
	virtual void undo_mallocs();
	bool empty();
	void clear();
	void flush();

	malloc_record* mallocs;
	void *operator new(size_t sz){
		return pstm_pmalloc(sz);
	}
	void* operator new[](size_t sz){
		return pstm_pmalloc(sz);
	}
	void operator delete(void *p){
		pstm_pfree(p);
	}
	void operator delete[](void* p){
		pstm_pfree(p);
	}
	mallocset();
	virtual ~mallocset();
};

#endif
