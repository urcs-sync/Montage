AR=ar
CXX=g++
CXXFLAGS=-std=c++17
CXXFLAGS+=-ggdb
CXXFLAGS+=-fno-stack-protector
#CXXFLAGS+=-O3
LDFLAGS=-I./ -pthread -lpmem -luuid
# TARGET=libsavitar.a

all: libsavitar.a libsavitar_sync.a libsavitar_buf.a

ifdef DEBUG
CXXFLAGS+=-DDEBUG
endif

ifdef LOG_SIZE
CXXFLAGS+=-DLOG_SIZE="((off_t)$(LOG_SIZE) << 30)"
else
CXXFLAGS+=-DLOG_SIZE="((off_t)32 << 30)"
endif

ifdef DISABLE_HT_PINNING
CXXFLAGS+=-DNO_HT_PINNING
endif

# Comment out since we generate sync version of libsavitar separately
# ifdef PRONTO_SYNC
# CXXFLAGS+=-DSYNC_SL # no ASL
# endif

libsavitar.a: thread.o persister.o nv_log.o nv_object.o context.o cpu_info.o nv_catalog.o nvm_manager.o nv_factory.o ckpt_alloc.o snapshot.o
	$(AR) rvs $@ $^

ckpt_alloc.o: ckpt_alloc.cpp ckpt_alloc.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

snapshot.o: snapshot.cpp snapshot.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

thread.o: thread.cpp thread.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

persister.o: persister.cpp persister.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

nv_log.o: nv_log.cpp nv_log.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

nv_object.o: nv_object.cpp nv_object.hpp recovery_context.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

context.o: context.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $^

cpu_info.o: cpu_info.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $^

nv_catalog.o: nv_catalog.cpp nv_catalog.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

nvm_manager.o: nvm_manager.cpp nvm_manager.hpp recovery_context.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

nv_factory.o: nv_factory.cpp nv_factory.hpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

libsavitar_sync.a: thread_sync.o persister_sync.o nv_log_sync.o nv_object_sync.o context_sync.o cpu_info_sync.o nv_catalog_sync.o nvm_manager_sync.o nv_factory_sync.o ckpt_alloc_sync.o snapshot_sync.o
	$(AR) rvs $@ $^

ckpt_alloc_sync.o: ckpt_alloc.cpp ckpt_alloc.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

snapshot_sync.o: snapshot.cpp snapshot.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

thread_sync.o: thread.cpp thread.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

persister_sync.o: persister.cpp persister.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

nv_log_sync.o: nv_log.cpp nv_log.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

nv_object_sync.o: nv_object.cpp nv_object.hpp recovery_context.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

context_sync.o: context.cpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $^

cpu_info_sync.o: cpu_info.cpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $^

nv_catalog_sync.o: nv_catalog.cpp nv_catalog.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

nvm_manager_sync.o: nvm_manager.cpp nvm_manager.hpp recovery_context.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

nv_factory_sync.o: nv_factory.cpp nv_factory.hpp
	$(CXX) -c $(CXXFLAGS) -DSYNC_SL -O3 -o $@ $<

libsavitar_buf.a: thread_buf.o persister_buf.o nv_log_buf.o nv_object_buf.o context_buf.o cpu_info_buf.o nv_catalog_buf.o nvm_manager_buf.o nv_factory_buf.o ckpt_alloc_buf.o snapshot_buf.o
	$(AR) rvs $@ $^

ckpt_alloc_buf.o: ckpt_alloc.cpp ckpt_alloc.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

snapshot_buf.o: snapshot.cpp snapshot.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

thread_buf.o: thread.cpp thread.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

persister_buf.o: persister.cpp persister.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

nv_log_buf.o: nv_log.cpp nv_log.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

nv_object_buf.o: nv_object.cpp nv_object.hpp recovery_context.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

context_buf.o: context.cpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $^

cpu_info_buf.o: cpu_info.cpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $^

nv_catalog_buf.o: nv_catalog.cpp nv_catalog.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

nvm_manager_buf.o: nvm_manager.cpp nvm_manager.hpp recovery_context.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

nv_factory_buf.o: nv_factory.cpp nv_factory.hpp
	$(CXX) -c $(CXXFLAGS) -DPRONTO_BUF -o $@ $<

clean:
	rm -f *.o
	rm -f *.a
