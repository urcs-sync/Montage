MONTAGE_PATH=../../
CC=g++
CFLAGS=-std=c++17 -O3 -Wall -pthread -I./ -DTCD -g -m64 -I$(MONTAGE_PATH)/ext/ralloc/src 
CFLAGS += -I$(MONTAGE_PATH)/src -I$(MONTAGE_PATH)/src/utils -I$(MONTAGE_PATH)/src/rideables -I$(MONTAGE_PATH)/src/test -I$(MONTAGE_PATH)/src/persist -I$(MONTAGE_PATH)/src/persist/api
CFLAGS += -I$(MONTAGE_PATH)/ext/tbb/include
#CFLAGS=-std=c++11 -O0 -g -Wall -pthread -I./ -DTCD -g -m64
LDFLAGS= -lpthread -levent \
	 -L../threadcached/obj \
	 -ldl -ljemalloc -lhwloc -lgomp
SUBDIRS=core db cache_test
SUBSRCS=$(wildcard core/*.cc) $(wildcard db/*.cc) $(wildcard cache_test/*.cc)
OBJECTS=$(SUBSRCS:.cc=.o)
EXEC=ycsbc

# Montage and ralloc
ifeq ($(OPT),montage)
	CFLAGS += -DMONTAGE
	CFLAGS += -DRALLOC
	LDFLAGS += -latomic
	LDFLAGS += -lthreadcached_montage
	LDFLAGS += -L$(MONTAGE_PATH)/lib -lMontage
	LDFLAGS += -L$(MONTAGE_PATH)/ext/ralloc -lralloc
endif

# transient on nvm, with ralloc 
ifeq ($(OPT),nvm)
	CFLAGS += -DRALLOC
	LDFLAGS += -lthreadcached_nvm
	LDFLAGS += -L$(MONTAGE_PATH)/ext/ralloc -lralloc
endif

# transient on dram, with jemalloc
ifeq ($(OPT),dram)
	CFLAGS += -DJEMALLOC
	LDFLAGS += -lthreadcached
endif

# Montage

# Ralloc

all: $(SUBDIRS) $(EXEC)

$(SUBDIRS):
	$(MAKE) -C $@

$(EXEC): $(wildcard *.cc) $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done
	$(RM) $(EXEC)

.PHONY: $(SUBDIRS) $(EXEC) test
