CC=g++
#CFLAGS=-std=c++11 -m64 -c -Ofast -Wall -fgnu-tm $(MEMCACHED) 
CFLAGS=-std=c++11 -m64 -c -O3 -g -Wall -fgnu-tm $(MEMCACHED) 
INCLUDES=-I../../threadcached/include
SOURCES=$(wildcard *.cc)
OBJECTS=$(SOURCES:.cc=.o)

all: $(SOURCES) $(OBJECTS)

.cc.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	$(RM) $(OBJECTS)

