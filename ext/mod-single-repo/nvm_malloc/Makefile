CC := gcc
debug:   CFLAGS := -O0 -ggdb -fpic -Wall -I. -Iulib-svn/include -DHAS_CLWB -DLOGGING
release: CFLAGS := -O3 -fpic -Wall -I. -Iulib-svn/include -DHAS_CLWB
no_flush: CFLAGS := -O3 -fpic -Wall -I. -Iulib-svn/include -DHAS_CLWB -DIMMER_DISABLE_FLUSHING
LDFLAGS := -lpthread

SRCDIR := src
OBJDIR := objects
OBJECTS := util.o chunk.o object_table.o arena.o nvm_malloc.o
LIBNAME := libnvmmalloc.so

release: $(LIBNAME) # libnvmmallocnoflush.so libnvmmallocnofence.so libnvmmallocnone.so

debug: $(LIBNAME)

# no_flush: $(LIBNAME)

$(LIBNAME): ulib-svn/lib/libulib.a $(addprefix $(OBJDIR)/, $(OBJECTS))
	$(CC) $(CFLAGS) -shared -o $@ $(addprefix $(OBJDIR)/, $(OBJECTS)) ulib-svn/lib/libulib.a $(LDFLAGS) -DPMEM_ENABLED

libnvmmallocnoflush.so: $(SRCDIR)/*.c ulib-svn/lib/libulib.a
	$(CC) $(CFLAGS) -shared -o $@ -DNOFLUSH $+ ulib-svn/lib/libulib.a $(LDFLAGS) -DPMEM_ENABLED

libnvmmallocnofence.so: $(SRCDIR)/*.c ulib-svn/lib/libulib.a
	$(CC) $(CFLAGS) -shared -o $@ -DNOFENCE $+ ulib-svn/lib/libulib.a $(LDFLAGS) -DPMEM_ENABLED

libnvmmallocnone.so: $(SRCDIR)/*.c ulib-svn/lib/libulib.a
	$(CC) $(CFLAGS) -shared -o $@ -DNOFLUSH -DNOFENCE $+ ulib-svn/lib/libulib.a $(LDFLAGS) -DPMEM_ENABLED

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/*.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $< -DPMEM_ENABLED

ulib-svn/lib/libulib.a:
	cd ulib-svn; make release

clean:
	@rm -f $(LIBNAME) libnvmmallocnoflush.so libnvmmallocnofence.so libnvmmallocnone.so
	@rm -rf $(OBJDIR)

.PHONY: test debug release
