name = libcdj
tmp_dir := /tmp/$(name)_debbuild
arch != uname -m

contents := $(shell test -f deploy/DEBIAN/control || sudo deploy/build-deb.sh)
version != grep 'Version:' deploy/DEBIAN/control | awk '{print $$2}'

CFLAGS = -fPIC -DPIC -Wall -Werror

LIBCDJ_SRC = src/c/cdj.c
LIBCDJ_DEP = src/c/cdj.c src/c/cdj.h

LIBVDJ_SRC = src/c/vdj.c
LIBVDJ_DEP = src/c/vdj.c src/c/vdj.h 

VDJ_SRC = src/c/vdj.c
VDJ_DEP = src/c/vdj.c src/c/vdj.h

OBJS = target/cdj.o target/vdj_net.o target/vdj_beat.o target/vdj_master.o target/vdj.o

all: target target/libcdj.a target/libcdj.so target/libvdj.a target/libvdj.so target/cdj-mon target/vdj-debug target/vdj

target:
	mkdir -p target

# Applications

target/vdj-debug: $(OBJS) target/vdj_debug.o
	gcc $(CFLAGS) -o $@ $(OBJS) target/vdj_debug.o -lpthread

target/vdj: $(OBJS) target/vdj_main.o
	gcc $(CFLAGS) -o $@ $(OBJS) target/vdj_main.o -lpthread

target/cdj-mon: $(OBJS) target/cdj_mon.o
	gcc $(CFLAGS) -o $@ $(OBJS) target/cdj_mon.o -lpthread

# Objects

# Each .c is compiled to a .o in target/

target/vdj_debug.o: src/c/vdj_debug.c
	gcc $(CFLAGS) src/c/vdj_debug.c -c -o $@

target/vdj_main.o: src/c/vdj_main.c
	gcc $(CFLAGS) src/c/vdj_main.c -c -o $@

target/vdj_net.o: src/c/vdj_net.c src/c/vdj_net.h
	gcc $(CFLAGS) src/c/vdj_net.c -c -o $@

target/vdj_beat.o: src/c/vdj_beat.c src/c/vdj_beat.h
	gcc $(CFLAGS) src/c/vdj_beat.c -c -o $@

target/vdj_master.o: src/c/vdj_master.c src/c/vdj_master.h
	gcc $(CFLAGS) src/c/vdj_master.c -c -o $@

target/vdj.o: $(VDJ_DEP)
	gcc $(CFLAGS) $(VDJ_SRC) -c -o $@

target/cdj_mon.o: src/c/cdj_mon.c src/c/cdj_mon_tui.c
	gcc $(CFLAGS) src/c/cdj_mon.c -c -o $@

target/cdj.o: $(LIBCDJ_DEP)
	gcc $(CFLAGS) $(LIBCDJ_SRC) -c -o $@

target/libcdj.so: $(LIBCDJ_DEP)
	gcc $(CFLAGS) -shared -rdynamic -o $@ $(LIBCDJ_SRC)

target/libcdj.a: $(LIBCDJ_DEP)
	gcc $(CFLAGS) -c -o $@ $(LIBCDJ_SRC)

target/libvdj.so: $(OBJS) $(LIBVDJ_DEP)
	gcc $(CFLAGS) -shared -rdynamic -o $@ $(LIBVDJ_SRC)

target/libvdj.a: $(OBJS) $(LIBVDJ_DEP)
	gcc $(CFLAGS) -c -o $@ $(LIBVDJ_SRC)

.PHONY: clean install uninstall deb test

test:
	mkdir -p target/
	gcc -Wall -Isrc/c -o target/libcdj_test src/test/test_libcdj.c target/libcdj.a
	target/libcdj_test

clean:
	rm -rf target/

install:
	cp libcdj.so /usr/lib64/

uninstall:
	rm -f /usr/lib64/libcdj.so

deb:
	sudo deploy/build-deb.sh
