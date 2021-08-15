#!/bin/sh

# This is the command which actually builds the cli executable - Raffi
g++ clevel_hash_cli.cpp -I ../../include -lpmemobj -pthread -I ../common ../test_backtrace.c ../valgrind_internal.cpp -o clevel_hash_cli
