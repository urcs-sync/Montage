#ifndef RSET_HPP
#define RSET_HPP

#include "Rideable.hpp"

template <typename K> class RSet : public virtual Rideable{
public:
	// Gets if a key is in the set.
	// returns : true if key exists, false otherwise
	virtual bool get(K key, int tid)=0;

	// Puts a key into the set. Always succeeds.
	virtual void put(K key, int tid)=0;

	// Inserts a new key into the map
	// if the key is not already present
	// returns : true if the insert is successful, false otherwise
	virtual bool insert(K key, int tid)=0;

	// Removes a key
	// returns : true if key exists, false otherwise
	virtual bool remove(K key, int tid)=0;
};

#endif