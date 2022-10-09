name = libcdj
tmp_dir := /tmp/$(name)_debbuild
arch != uname -m

ifeq ($(arch),x86_64)
  LIBDIR=/usr/lib
else ifeq ($(arch),armv6l)
  LIBDIR=/usr/lib
else ifeq ($(arch),armv7l)
  LIBDIR=/usr/lib
endif

version != cat version | awk -F= '{print $$2}'

CC = gcc
CFLAGS = -fPIC -DPIC -Wall -Werror

LIBCDJ_SRC = src/c/cdj.c
LIBCDJ_DEP = src/c/cdj.c src/c/cdj.h

LIBVDJ_SRC = src/c/vdj.c
LIBVDJ_DEP = src/c/vdj.c src/c/vdj.h 

VDJ_SRC = src/c/vdj.c
VDJ_DEP = src/c/vdj.c src/c/vdj.h

OBJS = target/cdj.o target/vdj_store.o target/vdj_net.o target/vdj_beatout.o target/vdj_master.o \
       target/vdj_discovery.o target/vdj_pselect.o target/vdj_simple.o \
       target/vdj.o

all: target target/libcdj.so target/libvdj.so \
     target/cdj-mon target/cdj-scan target/vdj-mon target/vdj-debug target/vdj target/vdj-1

target:
	mkdir -p target

# Applications

target/vdj-debug: $(OBJS) target/vdj_debug.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/vdj_debug.o -lpthread

target/vdj: $(OBJS) target/vdj_main.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/vdj_main.o -lpthread

target/cdj-mon: $(OBJS) target/cdj_mon.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/cdj_mon.o -lpthread

target/cdj-scan: $(OBJS) target/cdj_scan.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/cdj_scan.o -lpthread

target/vdj-mon: $(OBJS) target/vdj_mon.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/vdj_mon.o -lpthread

target/vdj-1: $(OBJS) target/vdj_1.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/vdj_1.o -lpthread

# Objects

# Each .c is compiled to a .o in target/

target/vdj_debug.o: src/c/vdj_debug.c
	$(CC) $(CFLAGS) src/c/vdj_debug.c -c -o $@

target/vdj_mon.o: src/c/vdj_mon.c
	$(CC) $(CFLAGS) src/c/vdj_mon.c -c -o $@

target/cdj_scan.o: src/c/cdj_scan.c
	$(CC) $(CFLAGS) src/c/cdj_scan.c -c -o $@

target/vdj_main.o: src/c/vdj_main.c
	$(CC) $(CFLAGS) src/c/vdj_main.c -c -o $@

target/vdj_net.o: src/c/vdj_net.c src/c/vdj_net.h
	$(CC) $(CFLAGS) src/c/vdj_net.c -c -o $@

target/vdj_beatout.o: src/c/vdj_beatout.c src/c/vdj_beatout.h
	$(CC) $(CFLAGS) src/c/vdj_beatout.c -c -o $@

target/vdj_master.o: src/c/vdj_master.c src/c/vdj_master.h
	$(CC) $(CFLAGS) src/c/vdj_master.c -c -o $@

target/vdj_discovery.o: src/c/vdj_discovery.c src/c/vdj_discovery.h
	$(CC) $(CFLAGS) src/c/vdj_discovery.c -c -o $@

target/vdj_pselect.o: src/c/vdj_pselect.c src/c/vdj_pselect.h
	$(CC) $(CFLAGS) src/c/vdj_pselect.c -c -o $@

target/vdj_store.o: src/c/vdj_store.c src/c/vdj_store.h
	$(CC) $(CFLAGS) src/c/vdj_store.c -c -o $@

target/vdj_simple.o: src/c/vdj_simple.c src/c/vdj_simple.h
	$(CC) $(CFLAGS) src/c/vdj_simple.c -c -o $@

target/vdj_1.o: src/c/vdj_1.c
	$(CC) $(CFLAGS) src/c/vdj_1.c -c -o $@

target/vdj.o: $(VDJ_DEP)
	$(CC) $(CFLAGS) $(VDJ_SRC) -c -o $@

target/cdj_mon.o: src/c/cdj_mon.c src/c/cdj_mon_tui.c
	$(CC) $(CFLAGS) src/c/cdj_mon.c -c -o $@

target/cdj.o: $(LIBCDJ_DEP)
	$(CC) $(CFLAGS) $(LIBCDJ_SRC) -c -o $@

target/libcdj.so: $(LIBCDJ_DEP) $(OBJS)
	$(CC) $(CFLAGS) -shared -rdynamic -o $@ $(OBJS)

target/libvdj.so: $(OBJS) $(LIBVDJ_DEP)
	$(CC) $(CFLAGS) -shared -rdynamic -o $@ $(LIBVDJ_SRC)

.PHONY: clean install uninstall deb test

rpc:
	cd src/x; rpcgen -Sc mount.x
	cd src/x; rpcgen nfs.x
	cd src/x; $(CC) -fPIC -DPIC -Wall -c mount_clnt.c
	cd src/x; $(CC) -fPIC -DPIC -Wall -c nfs_clnt.c

	# hack 
	# multiple definitions of `xdr_FHandle', so had to manually hack the following
	#cd src/x; $(CC) -fPIC -DPIC -Wall -c mount_xdr.c
	#cd src/x; $(CC) -fPIC -DPIC -Wall -c nfs_xdr.c

	cd src/x; $(CC) -fPIC -DPIC -Wall -c xdr.c

rpc-list:
	cd src/x; $(CC) -fPIC -DPIC -Wall -c vdj_list_exports.c
	cd src/x; $(CC) -fPIC -DPIC -Wall -o list-exports xdr.o mount_clnt.o nfs_clnt.o vdj_list_exports.o
	src/x/list-exports 169.254.177.253

rpc-readdir:
	cd src/x; $(CC) -fPIC -DPIC -Wall -c vdj_nfs_explore.c
	cd src/x; $(CC) -fPIC -DPIC -Wall -o read-dir xdr.o mount_clnt.o nfs_clnt.o vdj_nfs_explore.o
	src/x/read-dir 169.254.177.253

test:
	mkdir -p target/
	$(CC) -Wall -Wno-unused-variable -Isrc/c -c src/test/test_libcdj.c -o target/test_libcdj.o
	$(CC) -Wall -Isrc/c -o target/libcdj_test target/test_libcdj.o target/cdj.o
	target/libcdj_test
	sniprun src/test/libcdj_pkts_test.c.snip
	sniprun src/test/bpm_madness_test.c.snip
	sniprun src/test/time_diff_test.c.snip

clean:
	rm -rf target/
	rm -f src/test/*.o

install:
	cp -f target/libcdj.so $(LIBDIR)/libcdj.so.1.0
	cp -f target/libvdj.so $(LIBDIR)/libvdj.so.1.0
	cd $(LIBDIR); test -h libcdj.so || ln -s libcdj.so.1.0 libcdj.so
	cd $(LIBDIR); test -h libvdj.so || ln -s libvdj.so.1.0 libvdj.so
	mkdir -p /usr/include/cdj
	cp -f src/c/*.h  /usr/include/cdj
	cp -f target/vdj-mon /usr/bin
	cp -f target/cdj-mon /usr/bin
	cp -f target/cdj-scan /usr/bin

uninstall:
	rm -f $(LIBDIR)/libcdj.so.1.0 $(LIBDIR)/libvdj.so.1.0 $(LIBDIR)/libcdj.so $(LIBDIR)/libvdj.so
	rm -rf /usr/include/cdj
	rm /usr/bin/vdj-mon
	rm /usr/bin/cdj-mon
	rm /usr/bin/cdj-scan

deb:
	sudo deploy/build-deb.sh
