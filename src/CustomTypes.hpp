#ifndef CUSTOMTYPES_HPP
#define CUSTOMTYPES_HPP

#include <stdio.h>
#include <string.h>
#include <string>
#include <iostream>

#include "HarnessUtils.hpp"

using namespace std;

#define NUMSTRING_LEN 10

// #define TYPE NumString
// #define TYPE string
#define TYPE int

class NumString{
	char* str = nullptr;
	void init(int x);
public:
	static int length;
	NumString();
	NumString(const int x);
	NumString(const uint64_t x);
	NumString(const NumString& oth);
	~NumString();

	void operator = (const NumString& oth);
	void operator = (const int x);
	bool operator == (const NumString& oth);
	bool operator < (const NumString& oth);
	bool operator > (const NumString& oth);
	bool operator <= (const NumString& oth);
	bool operator >= (const NumString& oth);
	bool operator != (const NumString& oth);

	int val();
	int size();
	const char* c_str();

	friend ostream& operator<<(ostream& os, const NumString& ns);
};

ostream& operator<<(ostream& os, const NumString& ns);

#endif