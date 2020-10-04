//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

#ifndef MURMURHASH3_H
#define MURMURHASH3_H

//-----------------------------------------------------------------------------
// Platform-specific functions and macros
#include <stdint.h>
#include <stddef.h>
#include "HarnessUtils.hpp" // for errexit
#include "CustomTypes.hpp"

//-----------------------------------------------------------------------------

uint32_t MurmurHash3_x86_32(const void *key, size_t length);


template<typename K>
inline uint32_t MurmurHash3(K key){
	errexit("MurmurHash3 not implemented for current key type.");
    return 0;
}
template<>
inline uint32_t MurmurHash3(int& key){
	// const char* buffer =_itoa(key);
	string buffer = std::to_string(key);
	return MurmurHash3_x86_32(buffer.c_str(), buffer.size());
}
template<>
inline uint32_t MurmurHash3(NumString& key){
	return MurmurHash3_x86_32(key.c_str(), key.size());
}


//-----------------------------------------------------------------------------

#endif // MURMURHASH3_H
