#include <iostream>
#include "../util/bloomfilter.hpp"

void check(filter a, filter b){
	if (a.intersect(b)){
		std::cout << "There is an intersection!\n";
	}else{
		std::cout << "There is no intersection!\n";
	}
	return;
}

int main(){
	filter a,b;
	int foo[1];

	check(a,b); // No intersection

	a.add(&foo[1]); // Add 1 to a
	a.add(&foo[5]); // Add 5 to a
	b.add(&foo[3]); // Add 3 to b

	check(a,b); // No intersection

	b.add(&foo[5]); // Add 5 to b

	check(a,b); // Intersection

	if(a.contains(&foo[1])){ // A should be empty
		std::cout << "Filter a contains a particular element before being cleared.\n";
	}else{
		std::cout << "ERROR, filter a is supposed to contain an element which it seems to not contain!\n";
	}

	a.clear(); // Reset a to empty

	check(a,b); // No intersection

	if(a.contains(&foo[1])){ // A should be empty
		std::cout << "ERROR, filter a is supposed to be empty!\n";
	}else{
		std::cout << "Fortunately, a does not contain a particular element after being cleared.\n";
	}

	return 0;
}
