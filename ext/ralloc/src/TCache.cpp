/*
 * Copyright (C) 2019 University of Rochester. All rights reserved.
 * Licenced under the MIT licence. See LICENSE file in the project root for
 * details. 
 */

/*
 * Copyright (C) 2018 Ricardo Leite
 * Licenced under the MIT licence. This file shares some portion from
 * LRMalloc(https://github.com/ricleite/lrmalloc) and its copyright 
 * is retained. See LICENSE for details about MIT License.
 */
#include <atomic>

#include "TCache.hpp"
// using namespace ralloc;
// thread_local TCaches ralloc::t_caches;
// std::atomic<uint64_t> ralloc::thd_cnt;

// TCaches::TCaches(){
// 	Ralloc::thd_cnt.fetch_add(1);
// };

// TCaches::~TCaches(){
// 	Ralloc::public_flush_cache();
// 	Ralloc::thd_cnt.fetch_sub(1,std::memory_order_relaxed);
// }
TCaches::TCaches():t_cache(){ };
TCaches::~TCaches(){};
void TCacheBin::push_block(char* block)
{
	// block has at least sizeof(char*)
	*(pptr<char>*)block = _block;
	_block = block;
	_block_num++;
}

void TCacheBin::push_list(char* block, uint32_t length)
{
	// caller must ensure there's no available block
	// this op is only used to fill empty cache
	assert(_block_num == 0);

	_block = block;
	_block_num = length;
}

char* TCacheBin::pop_block()
{
	// caller must ensure there's an available block
	assert(_block_num > 0);

	char* ret = _block;
	char* next = (char*)(*(pptr<char>*)ret);
	_block = next;
	_block_num--;
	return ret;
}

void TCacheBin::pop_list(char* block, uint32_t length)
{
	assert(_block_num >= length);

	_block = block;
	_block_num -= length;
}
