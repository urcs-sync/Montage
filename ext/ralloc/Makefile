##
# Copyright (C) 2019 University of Rochester. All rights reserved.
# Licenced under the MIT licence. See LICENSE file in the project root for
# details. 
##

SRC = ../src
CXX = g++

WARNING_FLAGS:=-ftrapv -Wreturn-type -W -Wall \
-Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-parameter

FLAGS = -O3 -g -fpermissive $(WARNING_FLAGS) -fno-omit-frame-pointer -fPIC -faligned-new #-DSHM_SIMULATING #-DDESTROY -DMEM_CONSUME_TEST
RALLOC_FLAGS = $(FLAGS) -DRALLOC -L.

CXXFLAGS = $(RALLOC_FLAGS) -ljemalloc 

LIBS = -pthread -lstdc++ -latomic 

all: libralloc.a

./obj/%.o: ./src/%.cpp
	@mkdir -p $(@D)
	$(CXX) -I $(SRC) -o $@ -c $^ $(CXXFLAGS)

libralloc.a:./obj/SizeClass.o ./obj/RegionManager.o ./obj/TCache.o ./obj/BaseMeta.o ./obj/ralloc.o
	ar -rcs $@ $^

clean:
	rm -rf ./obj/*
	rm -f libralloc.a
	rm -rf /mnt/pmem/*
