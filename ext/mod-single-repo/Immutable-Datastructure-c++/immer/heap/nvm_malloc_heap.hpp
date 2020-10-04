//
// immer: immutable data structures for C++
// Copyright (C) 2016, 2017, 2018 Juan Pedro Bolivar Puente
//
// This software is distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://boost.org/LICENSE_1_0.txt
//

#pragma once

#include <immer/config.hpp>

#include <memory>
#include <cstdlib>
#include <iostream>
#include <execinfo.h>
#include <cxxabi.h> 

#include <nvm_malloc.h>

namespace immer {

/** Print a demangled stack backtrace of the caller function to FILE* out. */
/*
static inline void print_stacktrace(FILE *out = stdout, unsigned int max_frames = 63)
{
    fprintf(out, "stack trace:\n");

    // storage array for stack trace address data
    void* addrlist[max_frames+1];

    // retrieve current stack addresses
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0) {
	fprintf(out, "  <empty, possibly corrupt>\n");
	return;
    }

    // resolve addresses into strings containing "filename(function+address)",
    // this array must be free()-ed
    char** symbollist = backtrace_symbols(addrlist, addrlen);

    // allocate string which will be filled with the demangled function name
    size_t funcnamesize = 256;
    char* funcname = (char*)malloc(funcnamesize);

    // iterate over the returned symbol lines. skip the first, it is the
    // address of this function.
    for (int i = 1; i < addrlen; i++)
    {
	char *begin_name = 0, *begin_offset = 0, *end_offset = 0;

	// find parentheses and +address offset surrounding the mangled name:
	// ./module(function+0x15c) [0x8048a6d]
	for (char *p = symbollist[i]; *p; ++p)
	{
	    if (*p == '(')
		begin_name = p;
	    else if (*p == '+')
		begin_offset = p;
	    else if (*p == ')' && begin_offset) {
		end_offset = p;
		break;
	    }
	}

	if (begin_name && begin_offset && end_offset
	    && begin_name < begin_offset)
	{
	    *begin_name++ = '\0';
	    *begin_offset++ = '\0';
	    *end_offset = '\0';

	    // mangled name is now in [begin_name, begin_offset) and caller
	    // offset in [begin_offset, end_offset). now apply
	    // __cxa_demangle():

	    int status;
	    char* ret = abi::__cxa_demangle(begin_name,
					    funcname, &funcnamesize, &status);
	    if (status == 0) {
		funcname = ret; // use possibly realloc()-ed string
		fprintf(out, "  %s : %s+%s\n",
			symbollist[i], funcname, begin_offset);
	    }
	    else {
		// demangling failed. Output function name as a C function with
		// no arguments.
		fprintf(out, "  %s : %s()+%s\n",
			symbollist[i], begin_name, begin_offset);
	    }
	}
	else
	{
	    // couldn't parse the line? print the whole line.
	    fprintf(out, "  %s\n", symbollist[i]);
	}
    }

    free(funcname);
    free(symbollist);
}
*/
/*!
 * A heap that uses `std::malloc` and `std::free` to manage memory.
 */
struct nvm_malloc_heap
{
    /*!
     * Returns a pointer to a memory region of size `size`, if the
     * allocation was successful and throws `std::bad_alloc` otherwise.
     */
    template <typename... Tags>
    static void* allocate(std::size_t size, Tags...)
    {
        if (size == 0) {
            return nullptr;
        }
        auto p = nvm_reserve(size);
        if (IMMER_UNLIKELY(!p))
            throw std::bad_alloc{};
//        std::cout << "Allocated:" << p << " (" << size << ")" << std::endl; 
//        print_stacktrace();
        return p;
    }

    /*!
     * Releases a memory region `data` that was previously returned by
     * `allocate`.  One must not use nor deallocate again a memory
     * region that once it has been deallocated.
     */
    static void deallocate(std::size_t size, void* data)
    {
//        std::cout << "Deallocated:" << data << " (" << size << ")" << std::endl; 
//        print_stacktrace();
        // TODO: Free is tricky since we need pointer links.
        nvm_free_size(data, size);
    }
};

} // namespace immer
