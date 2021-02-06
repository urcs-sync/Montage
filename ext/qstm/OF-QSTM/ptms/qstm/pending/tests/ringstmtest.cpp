#include <iostream>
#include "../ringstm.hpp"

void test_end(bool success){
	if(success) std::cout << "  Txn success\n";
	else std::cout << "  Txn failed\n";
}

//TODO Add checks for internal state of stm
//TODO Check multiple conflicting transactions
//TODO Convert to a better API
int main(){
	long *array = new long[10];
	long foo = 42;
	bool success;

	// Initialize a playground
	for(int i=0; i<10; i++) array[i] = i+2;

	// Initialize two transactions
	transaction txn_one;
	transaction txn_two;

	// Single transaction test (read, write, read, end)
	std::cout << std::endl << "Testing one read-write-read transaction\n";
	txn_one.tm_begin();

	std::cout << "  txn_one read " << (long)txn_one.tm_read((void**)array) << std::endl;
	txn_one.tm_write((void**)array, (void*)foo);
	std::cout  << "  txn_one read " << (long)txn_one.tm_read((void**)array) << std::endl;

	test_end(txn_one.tm_end());

	txn_one.tm_abort();

	// *************************************
	// Now a write-read conflict test: (not a real conflict)
	// *************************************
	std::cout << std::endl << "Testing write-read overlap\n";
	txn_one.tm_begin();
	txn_two.tm_begin();

	// First txn writes a value
	txn_one.tm_write((void**)array, (void*)foo);

	// Second txn reads the location and changes start time
	std::cout  << "  txn_two read " << (long)txn_two.tm_read((void**)array) << std::endl;

	// Both transactions succeed because the read transaction adjusts start time
	test_end(txn_one.tm_end());
	test_end(txn_two.tm_end());


	// *************************************
	// Now a test of conflicting writes:
	// *************************************
	std::cout << std::endl << "Testing write-write overlap\n";
	txn_one.tm_begin();
	txn_two.tm_begin();

	// Both txns write a value to the same address
	txn_one.tm_write((void**)array, (void*)foo);
	txn_two.tm_write((void**)array, (void*)foo);

	// One transaction will abort
	test_end(txn_one.tm_end());
	test_end(txn_two.tm_end());

	// *************************************
	// Now a test of a must-abort WRW conflict:
	// *************************************
	std::cout << std::endl << "Testing write-read-write overlap\n";
	txn_one.tm_begin();
	txn_two.tm_begin();

	// Both txns write a value to the same address but the second one reads it first
	txn_one.tm_write((void**)array, (void*)foo);
	std::cout  << "  txn_two read " << (long)txn_two.tm_read((void**)array) << std::endl;
	txn_two.tm_write((void**)array, (void*)foo);

	// Transaction two will abort because it read before the write but committed after it
	test_end(txn_one.tm_end());
	test_end(txn_two.tm_end());

	// *************************************
	// Now a test trickier test:
	// *************************************
	std::cout << std::endl << "Testing read-write-read overlap\n";
	txn_one.tm_begin();
	txn_two.tm_begin();

	// Both txns write a value to the same address but the second one reads it first
	std::cout  << "  txn_two read " << (long)txn_two.tm_read((void**)array) << std::endl;
	txn_one.tm_write((void**)array, (void*)foo);
	test_end(txn_one.tm_end());

	// This transaction will abort
	std::cout  << "  txn_two read " << (long)txn_two.tm_read((void**)array) << std::endl;
	test_end(txn_two.tm_end());

	// *************************************
	// *************************************
	std::cout << std::endl << "Testing read_a-read_b-write_a-write_b overlap\n";
	txn_one.tm_begin();
	txn_two.tm_begin();

	std::cout  << "  txn_one read " << (long)txn_one.tm_read((void**)array) << std::endl;
	std::cout  << "  txn_two read " << (long)txn_two.tm_read((void**)array) << std::endl;

	txn_one.tm_write((void**)array, (void*)foo);
	test_end(txn_one.tm_end());

	txn_two.tm_write((void**)array, (void*)foo);
	test_end(txn_two.tm_end());

	return 0;
}
