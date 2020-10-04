CC=g++
MEMCACHED=-DTCD
CFLAGS=-std=c++11 -m64 -c -O3 -Wall -fgnu-tm $(MEMCACHED) 
#CFLAGS=-std=c++11 -m64 -c -O0 -g -Wall -fgnu-tm $(MEMCACHED) 
INCLUDES=-I../ -I../../threadcached/include -I../../threadcached
SOURCES=$(wildcard *.cc)
OBJECTS=$(SOURCES:.cc=.o)

all: $(SOURCES) $(OBJECTS)

.cc.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	$(RM) $(OBJECTS)

