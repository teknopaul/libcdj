name = libcdj
tmp_dir := /tmp/$(name)_debbuild
arch != uname -m

version != cat version | awk -F= '{print $$2}'

CC = gcc
CFLAGS = -fPIC -DPIC -Wall -Werror

LIBCDJ_SRC = src/c/cdj.c
LIBCDJ_DEP = src/c/cdj.c src/c/cdj.h

LIBVDJ_SRC = src/c/vdj.c
LIBVDJ_DEP = src/c/vdj.c src/c/vdj.h 

VDJ_SRC = src/c/vdj.c
VDJ_DEP = src/c/vdj.c src/c/vdj.h

OBJS = target/cdj.o target/vdj_store.o target/vdj_net.o target/vdj_beat.o target/vdj_master.o target/vdj_discovery.o target/vdj.o

all: target target/libcdj.a target/libcdj.so target/libvdj.a target/libvdj.so target/cdj-mon target/vdj-mon target/vdj-debug target/vdj

target:
	mkdir -p target

# Applications

target/vdj-debug: $(OBJS) target/vdj_debug.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/vdj_debug.o -lpthread

target/vdj: $(OBJS) target/vdj_main.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/vdj_main.o -lpthread

target/cdj-mon: $(OBJS) target/cdj_mon.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/cdj_mon.o -lpthread

target/vdj-mon: $(OBJS) target/vdj_mon.o
	$(CC) $(CFLAGS) -o $@ $(OBJS) target/vdj_mon.o -lpthread

# Objects

# Each .c is compiled to a .o in target/

target/vdj_debug.o: src/c/vdj_debug.c
	$(CC) $(CFLAGS) src/c/vdj_debug.c -c -o $@

target/vdj_mon.o: src/c/vdj_mon.c
	$(CC) $(CFLAGS) src/c/vdj_mon.c -c -o $@

target/vdj_main.o: src/c/vdj_main.c
	$(CC) $(CFLAGS) src/c/vdj_main.c -c -o $@

target/vdj_net.o: src/c/vdj_net.c src/c/vdj_net.h
	$(CC) $(CFLAGS) src/c/vdj_net.c -c -o $@

target/vdj_beat.o: src/c/vdj_beat.c src/c/vdj_beat.h
	$(CC) $(CFLAGS) src/c/vdj_beat.c -c -o $@

target/vdj_master.o: src/c/vdj_master.c src/c/vdj_master.h
	$(CC) $(CFLAGS) src/c/vdj_master.c -c -o $@

target/vdj_discovery.o: src/c/vdj_discovery.c src/c/vdj_discovery.h
	$(CC) $(CFLAGS) src/c/vdj_discovery.c -c -o $@

target/vdj_store.o: src/c/vdj_store.c src/c/vdj_store.h
	$(CC) $(CFLAGS) src/c/vdj_store.c -c -o $@

target/vdj.o: $(VDJ_DEP)
	$(CC) $(CFLAGS) $(VDJ_SRC) -c -o $@

target/cdj_mon.o: src/c/cdj_mon.c src/c/cdj_mon_tui.c
	$(CC) $(CFLAGS) src/c/cdj_mon.c -c -o $@

target/cdj.o: $(LIBCDJ_DEP)
	$(CC) $(CFLAGS) $(LIBCDJ_SRC) -c -o $@

target/libcdj.so: $(LIBCDJ_DEP)
	$(CC) $(CFLAGS) -shared -rdynamic -o $@ $(LIBCDJ_SRC)

target/libcdj.a: $(LIBCDJ_DEP)
	$(CC) $(CFLAGS) -c -o $@ $(LIBCDJ_SRC)

target/libvdj.so: $(OBJS) $(LIBVDJ_DEP)
	$(CC) $(CFLAGS) -shared -rdynamic -o $@ $(LIBVDJ_SRC)

target/libvdj.a: $(OBJS) $(LIBVDJ_DEP)
	$(CC) $(CFLAGS) -c -o $@ $(LIBVDJ_SRC)

.PHONY: clean install uninstall deb test

test:
	mkdir -p target/
	$(CC) -Wall -Isrc/c -o target/libcdj_test src/test/test_libcdj.c target/libcdj.a
	target/libcdj_test
	sniprun src/test/libcdj_pkts_test.c.snip
	sniprun src/test/bpm_madness_test.c.snip

clean:
	rm -rf target/

install:
	cp target/libcdj.so target/libvdj.so /usr/lib/x86_64-linux-gnu/
	mkdir -p /usr/include/cdj
	cp src/c/*.h  /usr/include/cdj
	cp target/{vdj-mon,cdj-mon} /usr/bin

uninstall:
	rm -f /usr/lib/x86_64-linux-gnu/libcdj.so /usr/lib/x86_64-linux-gnu/libvdj.so
	rm -rf /usr/include/cdj
	rm /usr/bin/{vdj-mon,cdj-mon}

deb:
	sudo deploy/build-deb.sh
