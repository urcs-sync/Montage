MONTAGE_PATH=../../
CXX=g++
# ALLOC=r
# transient objects using jemalloc on DRAM
PROT_OBJ = obj/memcached.o obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o obj/pku_memcached.o obj/util.o\
	   obj/itoa_ljust.o
# transient objects using ralloc on NVM
PROT_OBJ_NVM = obj/memcached_nvm.o obj/murmur3_hash_nvm.o obj/items_nvm.o obj/assoc_nvm.o obj/thread_nvm.o \
	   obj/bipbuffer_nvm.o obj/crawler_nvm.o obj/pku_memcached_nvm.o obj/util_nvm.o\
	   obj/itoa_ljust_nvm.o 
# persistent objects using montage and ralloc on NVM
PROT_OBJ_MONTAGE = obj/memcached_m.o obj/murmur3_hash_m.o obj/items_m.o obj/assoc_m.o obj/thread_m.o \
	   obj/bipbuffer_m.o obj/crawler_m.o obj/pku_memcached_m.o obj/util_m.o\
	   obj/itoa_ljust_m.o 
libralloc=ralloc/test

#OPT_LEVEL = -O0 -g
OPT_LEVEL = -O3

ERROR     = -DFAIL_ASSERT
OPTS = -Iinclude/  -levent -DHAVE_CONFIG_H -Wall -std=c++17 \
       -fPIC $(OPT_LEVEL) $(ERROR) -I$(MONTAGE_PATH)/ext/ralloc/src \
       #-DUSE_HODOR -I./hodor/libhodor -I./hodor/include
OPTS += -I$(MONTAGE_PATH)/src -I$(MONTAGE_PATH)/src/utils -I$(MONTAGE_PATH)/src/rideables -I$(MONTAGE_PATH)/src/test -I$(MONTAGE_PATH)/src/persist -I$(MONTAGE_PATH)/src/persist/api
OPTS += -I$(MONTAGE_PATH)/ext/tbb/include

LIBS = obj/libthreadcached.a $(MONTAGE_PATH)/ext/ralloc/test/libralloc.a
LINKOPTS = -lpthread -levent -ldl -L$(MONTAGE_PATH)/ext/ralloc -ljemalloc #-lhodor
EXE = bin/server bin/end bin/get bin/insert
WARNING_FLAGS:=-ftrapv -Wreturn-type -W -Wall \
-Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-parameter
OPTS+=$(WARNING_FLAGS) 
LINKOPTS+=$(WARNING_FLAGS)
# Ralloc by default
# ifeq ($(ALLOC),r)
OPTS += -L$(MONTAGE_PATH)/ext/ralloc/test -lralloc
LINKOPTS += -L$(MONTAGE_PATH)/ext/ralloc/test -lralloc
# endif

# ifeq ($(ALLOC),mak)
# 	OPTS += -I./ralloc/ext/makalu_alloc/include -DMAKALU -L./ralloc/ext/makalu_alloc/lib -lmakalu
# 	LINKOPTS += -L./ralloc/ext/makalu_alloc/lib -lmakalu
# endif

# ifeq ($(ALLOC),je)
# 	OPTS += -DJEMALLOC
# endif

# ifeq ($(ALLOC),lr)
# 	# Ralloc without flush and fence is effectively LRMalloc, with optimization
# 	OPTS += -DRALLOC -DPWB_IS_NOOP -L./ralloc/test -lralloc
# 	LINKOPTS += -L./ralloc/test -lralloc
# endif

# ifeq ($(ALLOC),pmdk)
# 	OPTS += -DPMDK -lpmemobj
# 	LINKOPTS += -lpmemobj
# endif

.PHONY : all lib bin
lib: obj/libthreadcached.a obj/libthreadcached_nvm.a obj/libthreadcached_montage.a
all: $(EXE)
bin: bin/server
	mv $^ bin/memcached

bin/%: test/%.cc $(LIBS)
	$(CXX) $^ -o $@ $(OPTS) $(LINKOPTS)

obj/libthreadcached.a: $(PROT_OBJ)
	ar -rcs $@ $^

obj/libthreadcached_nvm.a: $(PROT_OBJ_NVM) $(MONTAGE_PATH)/ext/ralloc/libralloc.a
	ar -rcs $@ $^

obj/libthreadcached_montage.a: $(PROT_OBJ_MONTAGE) $(MONTAGE_PATH)/ext/ralloc/libralloc.a $(MONTAGE_PATH)/lib/libPDSHarness.a
	ar -rcs $@ $^

obj/%.o: src/%.cc
	@mkdir -p $(@D)
	$(CXX) -c $^ $(OPTS) -DJEMALLOC -o $@

obj/%_nvm.o: src/%.cc
	@mkdir -p $(@D)
	$(CXX) -c $^ $(OPTS) -DRALLOC -o $@

obj/%_m.o: src/%.cc
	@mkdir -p $(@D)
	$(CXX) -c $^ $(OPTS) -DRALLOC -DMONTAGE -o $@

.PHONY : clean
clean: 
	rm -f obj/* exec *.d /dev/shm/memcached* $(EXE) $(PERF_RUN) $(RPMA_RUN)
.PHONY : reset
reset:
	@rm -f /dev/shm/test*
	@rm -f /dev/shm/memcached*
	@rm -f /mnt/pmem/test*
	@rm -f /mnt/pmem/memcached*

# include/hodo-plib.h
