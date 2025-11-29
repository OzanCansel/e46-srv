.DEFAULT_GOAL := all

CC=gcc
CFLAGS=-g

build:
	mkdir build

build/example: build
	mkdir build/example

build/obj:
	mkdir build/obj

build/lib:
	mkdir build/lib

build/obj/e46srv.o: src/include/e46srv.h src/e46srv.c build/obj
	$(CC) $(CFLAGS) -I src/include -c src/e46srv.c -o build/obj/e46srv.o

build/lib/libe46srv.a: build/obj/e46srv.o build/lib
	$(AR) rcs build/lib/libe46srv.a build/obj/e46srv.o

lib: build/lib/libe46srv.a

build/example/e46-echo-srv: build/lib/libe46srv.a example/e46-echo-srv/main.c build/example
	$(CC) $(CFLAGS) -I src/include example/e46-echo-srv/main.c build/lib/libe46srv.a -o build/example/e46-echo-srv

build/example/e46-stress: example/e46-stress/main.c build/example
	$(CC) $(CFLAGS) example/e46-stress/main.c -o build/example/e46-stress

example: build/example/e46-echo-srv build/example/e46-stress

all: lib example

clean:
	rm -rf build/*