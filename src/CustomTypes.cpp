#include "CustomTypes.hpp"

void NumString::init(int x){
	if (length < 3){
		errexit("NumString length need to be larger than 3.");
	}
	if (!str){
		str = new char[length];
	}
	str[length-1] = 0;
	if (x < 0){
		str[0] = '-';
		x = -x;
	} else {
	    str[0] = '0';
	}
	int i = length - 2;
	for (; i > 0; i--){
		if (x == 0){
			break;
		}
		str[i] = '0'+(x % 10);
		x /= 10;
	}
	for (; i > 0; i--){
		str[i] = '0';
	}
	if (x != 0){
		errexit("NumString init overflow.");
	}
}

NumString::NumString(){
	init(0);
}
NumString::NumString(const int x){
	init(x);
}
NumString::NumString(const uint64_t x){
	init(x);
}
NumString::NumString(const NumString& oth){
	str = new char[length];
	strcpy(str, oth.str);
}
NumString::~NumString(){
	delete str;
}

void NumString::operator = (const NumString& oth){
	strcpy(str, oth.str);
}
void NumString::operator = (const int x){
	init(x);
}
bool NumString::operator == (const NumString& oth){
	return (atoi(str) == atoi(oth.str));
}
bool NumString::operator < (const NumString& oth){
	return (atoi(str) < atoi(oth.str));
}
bool NumString::operator > (const NumString& oth){
	return (atoi(str) > atoi(oth.str));
}
bool NumString::operator <= (const NumString& oth){
	return (atoi(str) <= atoi(oth.str));
}
bool NumString::operator >= (const NumString& oth){
	return (atoi(str) >= atoi(oth.str));
}
bool NumString::operator != (const NumString& oth){
	return (atoi(str) != atoi(oth.str));
}

int NumString::val(){
	return atoi(str);
}

const char* NumString::c_str(){
	return str;
}

int NumString::size(){
	return length-1;
}

ostream& operator<<(ostream& os, const NumString& ns){
    os << ns.str;
    return os;
}

int NumString::length = NUMSTRING_LEN;